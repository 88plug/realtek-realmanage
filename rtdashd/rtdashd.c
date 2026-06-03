#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <syslog.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <stdatomic.h>
#include <dirent.h>

#include "../librtdash/rtdash.h"

static volatile int running = 1;

/* Prometheus metrics — atomic for safe cross-thread access */
static _Atomic int metric_driver_ready_total = 0;
static _Atomic int metric_oob_messages_total = 0;
static _Atomic long metric_last_push_timestamp = 0;
static _Atomic int metric_hostname_syncs_total = 0;

/* Server socket for metrics endpoint (global so main can close it on exit) */
static int metrics_srv_fd = -1;

/* Self-pipe for SIGHUP */
static int sig_pipe[2] = { -1, -1 };

/* Metrics endpoint config */
static int metrics_port = 9101;
static int push_interval = 30;

static void sighandler(int sig)
{
	(void)sig;
	running = 0;
}

static void sighup_handler(int sig)
{
	(void)sig;
	char c = 1;
	if (sig_pipe[1] >= 0) {
		int saved = errno;
		ssize_t _r = write(sig_pipe[1], &c, 1);
		(void)_r;
		errno = saved;
	}
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

static int get_installed_packages(char *buf, size_t len)
{
	FILE *fp;
	int count = 0;
	char line[512];
	char pkg_name[256] = "";
	char pkg_ver[256] = "";

	buf[0] = '\0';

	fp = fopen("/var/lib/dpkg/status", "r");
	if (!fp) {
		/* try rpm-based fallback */
		if (access("/var/lib/rpm/Packages", F_OK) == 0) {
			strncpy(buf, "rpm-based", len - 1);
			buf[len - 1] = '\0';
			return 0;
		}
		return -1;
	}

	while (fgets(line, sizeof(line), fp) && count < 20) {
		line[strcspn(line, "\n")] = '\0';
		if (strncmp(line, "Package: ", 9) == 0) {
			strncpy(pkg_name, line + 9, sizeof(pkg_name) - 1);
			pkg_name[sizeof(pkg_name) - 1] = '\0';
		} else if (strncmp(line, "Version: ", 9) == 0) {
			strncpy(pkg_ver, line + 9, sizeof(pkg_ver) - 1);
			pkg_ver[sizeof(pkg_ver) - 1] = '\0';
			if (pkg_name[0]) {
				size_t cur = strlen(buf);
				size_t remain = len - cur;
				if (remain > 2) {
					snprintf(buf + cur, remain, "%s=%s;",
						 pkg_name, pkg_ver);
					count++;
				}
				pkg_name[0] = '\0';
				pkg_ver[0] = '\0';
			}
		}
	}

	fclose(fp);
	return count;
}

static void parse_config(const char *path, char *ifname, int *mp, int *pi)
{
	FILE *fp;
	char line[256];

	fp = fopen(path, "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		char key[64], val[192];

		line[strcspn(line, "\n")] = '\0';

		/* skip comments and empty lines */
		if (line[0] == '#' || line[0] == '\0')
			continue;

		if (sscanf(line, "%63[^=]=%191s", key, val) != 2)
			continue;

		if (strcmp(key, "INTERFACE") == 0) {
			snprintf(ifname, 16, "%.15s", val);
		} else if (strcmp(key, "METRICS_PORT") == 0) {
			int v = atoi(val);
			if (v >= 0 && v <= 65535)
				*mp = v;
		} else if (strcmp(key, "PUSH_INTERVAL") == 0) {
			int v = atoi(val);
			if (v > 0)
				*pi = v;
		}
	}

	fclose(fp);
}

/* Prometheus metrics HTTP server thread */
static void *metrics_thread_func(void *arg)
{
	int port = *(int *)arg;
	int srv, client;
	struct sockaddr_in addr;
	int optval = 1;

	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0) {
		syslog(LOG_ERR, "metrics: socket: %m");
		return NULL;
	}
	metrics_srv_fd = srv;

	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)port);

	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		syslog(LOG_ERR, "metrics: bind port %d: %m", port);
		close(srv);
		return NULL;
	}

	if (listen(srv, 8) < 0) {
		syslog(LOG_ERR, "metrics: listen: %m");
		close(srv);
		return NULL;
	}

	syslog(LOG_INFO, "metrics endpoint listening on port %d", port);

	while (running) {
		client = accept(srv, NULL, NULL);
		if (client < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		/* Read and discard request */
		char reqbuf[1024];
		{ ssize_t _r = read(client, reqbuf, sizeof(reqbuf)); (void)_r; }

		char body[2048];
		int blen = snprintf(body, sizeof(body),
			"# HELP rtdash_driver_ready_total Number of DRIVER_READY messages sent.\n"
			"# TYPE rtdash_driver_ready_total counter\n"
			"rtdash_driver_ready_total %d\n"
			"# HELP rtdash_oob_messages_total Number of OOB messages received.\n"
			"# TYPE rtdash_oob_messages_total counter\n"
			"rtdash_oob_messages_total %d\n"
			"# HELP rtdash_last_push_timestamp Unix timestamp of the last OS data push.\n"
			"# TYPE rtdash_last_push_timestamp gauge\n"
			"rtdash_last_push_timestamp %ld\n"
			"# HELP rtdash_hostname_syncs_total Number of hostname syncs performed.\n"
			"# TYPE rtdash_hostname_syncs_total counter\n"
			"rtdash_hostname_syncs_total %d\n",
			metric_driver_ready_total,
			metric_oob_messages_total,
			metric_last_push_timestamp,
			metric_hostname_syncs_total);

		char hdr[256];
		int hlen = snprintf(hdr, sizeof(hdr),
			"HTTP/1.0 200 OK\r\n"
			"Content-Type: text/plain; version=0.0.4\r\n"
			"\r\n");

		{ ssize_t _r = write(client, hdr, (size_t)hlen); (void)_r; }
		{ ssize_t _r = write(client, body, (size_t)blen); (void)_r; }
		close(client);
	}

	close(srv);
	return NULL;
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-i <interface>] [-f] [-m <port>] [-I <seconds>] [-h]\n", prog);
	fprintf(stderr, "  -i  Network interface (default: auto-detect)\n");
	fprintf(stderr, "  -f  Run in foreground\n");
	fprintf(stderr, "  -m  Prometheus metrics port (default: 9101, 0=disabled)\n");
	fprintf(stderr, "  -I  Push interval in seconds (default: 30)\n");
	fprintf(stderr, "  -h  Show help\n");
}

static int find_dash_interface(char *ifname, size_t len)
{
	DIR *d;
	struct dirent *ent;

	d = opendir("/sys/class/net");
	if (!d)
		return -1;

	struct rtdash_ctx probe;
	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		if (rtdash_open(&probe, ent->d_name) == 0) {
			if (rtdash_enable_diag(&probe) == 0) {
				bool capable = rtdash_is_dash_capable(&probe);
				rtdash_disable_diag(&probe);
				if (capable) {
					snprintf(ifname, len, "%.*s",
					         (int)(len - 1), ent->d_name);
					rtdash_close(&probe);
					closedir(d);
					return 0;
				}
			}
			rtdash_close(&probe);
		}
	}

	closedir(d);
	return -1;
}

static void set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[])
{
	char ifname[16] = "";
	int foreground = 0;
	int opt;

	/* Defaults applied by parse_config, then overridden by CLI */
	parse_config("/etc/rtdashd.conf", ifname, &metrics_port, &push_interval);

	while ((opt = getopt(argc, argv, "i:fm:I:h")) != -1) {
		switch (opt) {
		case 'i':
			snprintf(ifname, sizeof(ifname), "%s", optarg);
			break;
		case 'f':
			foreground = 1;
			break;
		case 'm':
			metrics_port = atoi(optarg);
			break;
		case 'I':
			push_interval = atoi(optarg);
			if (push_interval <= 0)
				push_interval = 30;
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

	/* Self-pipe for SIGHUP */
	if (pipe(sig_pipe) < 0) {
		syslog(LOG_ERR, "pipe: %m");
		return 1;
	}
	set_nonblock(sig_pipe[0]);
	set_nonblock(sig_pipe[1]);
	signal(SIGHUP, sighup_handler);

	/* Metrics endpoint thread */
	pthread_t mthr;
	if (metrics_port > 0) {
		if (pthread_create(&mthr, NULL, metrics_thread_func, &metrics_port) != 0)
			syslog(LOG_WARNING, "failed to start metrics thread");
		else
			syslog(LOG_INFO, "metrics thread started on port %d", metrics_port);
	}

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
	else {
		syslog(LOG_INFO, "sent DRIVER_READY on %s", ifname);
		metric_driver_ready_total++;
	}

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
	else
		metric_last_push_timestamp = (long)time(NULL);

	rtdash_sync_hostname(&ctx);
	metric_hostname_syncs_total++;

	/* Software inventory push */
	{
		char pkgbuf[4096];
		int npkg = get_installed_packages(pkgbuf, sizeof(pkgbuf));
		if (npkg >= 0 && pkgbuf[0]) {
			syslog(LOG_INFO, "pushing software inventory (%d packages)", npkg);
			/* Send as type 0x04 payload */
			rtdash_send(&ctx, pkgbuf, (uint16_t)strlen(pkgbuf), 0x04);
		}
	}

	syslog(LOG_INFO, "running on %s (push interval %ds)", ifname, push_interval);

	/* epoll-based main loop */
	int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (tfd < 0) {
		syslog(LOG_ERR, "timerfd_create: %m");
		rtdash_close(&ctx);
		return 1;
	}

	struct itimerspec its;
	its.it_value.tv_sec = push_interval;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = push_interval;
	its.it_interval.tv_nsec = 0;
	timerfd_settime(tfd, 0, &its, NULL);

	int epfd = epoll_create1(0);
	if (epfd < 0) {
		syslog(LOG_ERR, "epoll_create1: %m");
		close(tfd);
		rtdash_close(&ctx);
		return 1;
	}

	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = tfd;
	epoll_ctl(epfd, EPOLL_CTL_ADD, tfd, &ev);

	ev.events = EPOLLIN;
	ev.data.fd = sig_pipe[0];
	epoll_ctl(epfd, EPOLL_CTL_ADD, sig_pipe[0], &ev);

	struct epoll_event events[4];

	while (running) {
		int nfds = epoll_wait(epfd, events, 4, -1);
		if (nfds < 0) {
			if (errno == EINTR)
				continue;
			syslog(LOG_ERR, "epoll_wait: %m");
			break;
		}

		for (int i = 0; i < nfds; i++) {
			if (events[i].data.fd == tfd) {
				/* Timer fired: heartbeat push */
				uint64_t expirations;
				{ ssize_t _r = read(tfd, &expirations, sizeof(expirations)); (void)_r; }

				struct rtdash_msg msg;
				if (rtdash_recv(&ctx, &msg) == 0) {
					syslog(LOG_INFO, "received OOB message: type=0x%02x len=%u",
					       msg.type, msg.len);
					metric_oob_messages_total++;
				}

				get_hostname(hostname, sizeof(hostname));
				get_os_info(os_name, sizeof(os_name), os_ver, sizeof(os_ver));
				rtdash_push_os_data(&ctx, hostname, os_name, os_ver);
				metric_last_push_timestamp = (long)time(NULL);

			} else if (events[i].data.fd == sig_pipe[0]) {
				/* SIGHUP: immediate full re-push */
				char discard[16];
				while (read(sig_pipe[0], discard, sizeof(discard)) > 0)
					;

				syslog(LOG_INFO, "SIGHUP received, performing full re-push");

				get_hostname(hostname, sizeof(hostname));
				get_os_info(os_name, sizeof(os_name), os_ver, sizeof(os_ver));
				rtdash_push_os_data(&ctx, hostname, os_name, os_ver);
				metric_last_push_timestamp = (long)time(NULL);

				rtdash_sync_hostname(&ctx);
				metric_hostname_syncs_total++;

				char pkgbuf[4096];
				int npkg = get_installed_packages(pkgbuf, sizeof(pkgbuf));
				if (npkg >= 0 && pkgbuf[0])
					rtdash_send(&ctx, pkgbuf, (uint16_t)strlen(pkgbuf), 0x04);
			}
		}
	}

	close(epfd);
	close(tfd);
	close(sig_pipe[0]);
	close(sig_pipe[1]);

	syslog(LOG_INFO, "shutting down, sending DRIVER_EXIT");
	rtdash_driver_exit(&ctx);
	rtdash_close(&ctx);

	if (metrics_port > 0 && metrics_srv_fd >= 0) {
		/* Unblock the metrics thread's accept() by closing the server socket */
		close(metrics_srv_fd);
		metrics_srv_fd = -1;
		pthread_join(mthr, NULL);
	}

	closelog();
	return 0;
}
