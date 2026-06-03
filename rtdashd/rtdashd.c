#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <syslog.h>

#include "../librtdash/rtdash.h"

static volatile int running = 1;

static void sighandler(int sig)
{
	(void)sig;
	running = 0;
}

static void get_hostname(char *buf, size_t len)
{
	if (gethostname(buf, len) < 0)
		strncpy(buf, "unknown", len);
	buf[len - 1] = '\0';
}

static void get_os_info(char *name, size_t nlen, char *ver, size_t vlen)
{
	struct utsname u;
	if (uname(&u) == 0) {
		strncpy(name, u.sysname, nlen - 1);
		name[nlen - 1] = '\0';
		snprintf(ver, vlen, "%.60s %.60s", u.release, u.machine);
	} else {
		strncpy(name, "Linux", nlen - 1);
		name[nlen - 1] = '\0';
		strncpy(ver, "unknown", vlen - 1);
		ver[vlen - 1] = '\0';
	}
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-i <interface>] [-f] [-h]\n", prog);
	fprintf(stderr, "  -i  Network interface (default: auto-detect)\n");
	fprintf(stderr, "  -f  Run in foreground\n");
	fprintf(stderr, "  -h  Show help\n");
}

static int find_dash_interface(char *ifname, size_t len)
{
	FILE *fp;
	char line[256];

	fp = popen("ls /sys/class/net/ 2>/dev/null", "r");
	if (!fp)
		return -1;

	struct rtdash_ctx probe;
	while (fgets(line, sizeof(line), fp)) {
		line[strcspn(line, "\n")] = '\0';
		if (strlen(line) == 0)
			continue;

		if (rtdash_open(&probe, line) == 0) {
			if (rtdash_enable_diag(&probe) == 0) {
				bool capable = rtdash_is_dash_capable(&probe);
				rtdash_disable_diag(&probe);
				if (capable) {
					strncpy(ifname, line, len - 1);
					ifname[len - 1] = '\0';
					rtdash_close(&probe);
					pclose(fp);
					return 0;
				}
			}
			rtdash_close(&probe);
		}
	}

	pclose(fp);
	return -1;
}

int main(int argc, char *argv[])
{
	char ifname[16] = "";
	int foreground = 0;
	int opt;

	while ((opt = getopt(argc, argv, "i:fh")) != -1) {
		switch (opt) {
		case 'i':
			strncpy(ifname, optarg, sizeof(ifname) - 1);
			break;
		case 'f':
			foreground = 1;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return (opt == 'h') ? 0 : 1;
		}
	}

	if (ifname[0] == '\0') {
		fprintf(stderr, "rtdashd: auto-detecting DASH interface...\n");
		if (find_dash_interface(ifname, sizeof(ifname)) < 0) {
			fprintf(stderr, "rtdashd: no DASH-capable interface found\n");
			return 1;
		}
		fprintf(stderr, "rtdashd: found DASH interface: %s\n", ifname);
	}

	if (!foreground) {
		if (daemon(0, 0) < 0) {
			perror("rtdashd: daemon");
			return 1;
		}
	}

	openlog("rtdashd", LOG_PID | LOG_CONS, LOG_DAEMON);
	signal(SIGINT, sighandler);
	signal(SIGTERM, sighandler);

	struct rtdash_ctx ctx;
	if (rtdash_open(&ctx, ifname) < 0) {
		syslog(LOG_ERR, "failed to open %s", ifname);
		return 1;
	}

	if (rtdash_enable_diag(&ctx) < 0) {
		syslog(LOG_ERR, "failed to enable diagnostics on %s", ifname);
		rtdash_close(&ctx);
		return 1;
	}

	if (rtdash_driver_ready(&ctx) < 0)
		syslog(LOG_WARNING, "failed to send DRIVER_READY");
	else
		syslog(LOG_INFO, "sent DRIVER_READY on %s", ifname);

	if (rtdash_arm_req(&ctx) < 0)
		syslog(LOG_WARNING, "failed to arm OOB REQ listener");
	if (rtdash_arm_ack(&ctx) < 0)
		syslog(LOG_WARNING, "failed to arm OOB ACK listener");

	char hostname[128], os_name[64], os_ver[128];
	get_hostname(hostname, sizeof(hostname));
	get_os_info(os_name, sizeof(os_name), os_ver, sizeof(os_ver));

	syslog(LOG_INFO, "pushing OS data: %s / %s %s", hostname, os_name, os_ver);
	if (rtdash_push_os_data(&ctx, hostname, os_name, os_ver) < 0)
		syslog(LOG_WARNING, "failed to push OS data");

	rtdash_sync_hostname(&ctx);

	syslog(LOG_INFO, "running on %s (poll interval 30s)", ifname);

	while (running) {
		struct rtdash_msg msg;
		if (rtdash_recv(&ctx, &msg) == 0) {
			syslog(LOG_INFO, "received OOB message: type=0x%02x len=%u",
			       msg.type, msg.len);
		}

		sleep(30);

		/* re-push OS data periodically as heartbeat */
		get_hostname(hostname, sizeof(hostname));
		get_os_info(os_name, sizeof(os_name), os_ver, sizeof(os_ver));
		rtdash_push_os_data(&ctx, hostname, os_name, os_ver);
	}

	syslog(LOG_INFO, "shutting down, sending DRIVER_EXIT");
	rtdash_driver_exit(&ctx);
	rtdash_close(&ctx);
	closelog();
	return 0;
}
