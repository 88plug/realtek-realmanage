#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "rtdash.h"

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s -i <interface> <command> [args...]\n", prog);
	fprintf(stderr, "\nCommands:\n");
	fprintf(stderr, "  check                    Check if interface supports DASH\n");
	fprintf(stderr, "  driver-ready             Send DRIVER_READY signal\n");
	fprintf(stderr, "  driver-exit              Send DRIVER_EXIT signal\n");
	fprintf(stderr, "  sync-hostname            Sync hostname to firmware\n");
	fprintf(stderr, "  enable-dash              Enable DASH\n");
	fprintf(stderr, "  disable-dash             Disable DASH\n");
	fprintf(stderr, "  set-ipv4 <ip> <mask> <gw>  Set OOB IPv4 configuration\n");
	fprintf(stderr, "  get-ipv4                 Get OOB IPv4 configuration\n");
	fprintf(stderr, "  send <hex>               Send raw OOB data to firmware\n");
	fprintf(stderr, "  recv                     Receive OOB data from firmware\n");
}

int main(int argc, char *argv[])
{
	const char *ifname = NULL;
	int argi = 1;

	if (argc < 4) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[argi], "-i") == 0) {
		ifname = argv[argi + 1];
		argi += 2;
	}

	if (!ifname || argi >= argc) {
		usage(argv[0]);
		return 1;
	}

	const char *cmd = argv[argi++];

	struct rtdash_ctx ctx;
	if (rtdash_open(&ctx, ifname) < 0) {
		fprintf(stderr, "rtdash-ctl: failed to open %s\n", ifname);
		return 1;
	}

	if (rtdash_enable_diag(&ctx) < 0) {
		fprintf(stderr, "rtdash-ctl: failed to enable diagnostics on %s\n", ifname);
		fprintf(stderr, "  (requires root + out-of-tree Realtek driver with DASH support)\n");
		rtdash_close(&ctx);
		return 1;
	}

	int ret = 0;

	if (strcmp(cmd, "check") == 0) {
		if (rtdash_is_dash_capable(&ctx)) {
			printf("DASH: capable\n");
		} else {
			printf("DASH: not capable\n");
			ret = 1;
		}

	} else if (strcmp(cmd, "driver-ready") == 0) {
		ret = rtdash_driver_ready(&ctx);
		printf(ret == 0 ? "DRIVER_READY sent\n" : "failed\n");

	} else if (strcmp(cmd, "driver-exit") == 0) {
		ret = rtdash_driver_exit(&ctx);
		printf(ret == 0 ? "DRIVER_EXIT sent\n" : "failed\n");

	} else if (strcmp(cmd, "sync-hostname") == 0) {
		ret = rtdash_sync_hostname(&ctx);
		printf(ret == 0 ? "hostname synced\n" : "failed\n");

	} else if (strcmp(cmd, "enable-dash") == 0) {
		ret = rtdash_enable_dash(&ctx);
		printf(ret == 0 ? "DASH enabled\n" : "failed\n");

	} else if (strcmp(cmd, "disable-dash") == 0) {
		ret = rtdash_disable_dash(&ctx);
		printf(ret == 0 ? "DASH disabled\n" : "failed\n");

	} else if (strcmp(cmd, "set-ipv4") == 0) {
		if (argi + 2 >= argc) {
			fprintf(stderr, "usage: set-ipv4 <ip> <mask> <gw>\n");
			ret = 1;
		} else {
			uint32_t addr = inet_addr(argv[argi]);
			uint32_t mask = inet_addr(argv[argi + 1]);
			uint32_t gw   = inet_addr(argv[argi + 2]);
			ret = rtdash_set_ipv4(&ctx, addr, mask, gw);
			printf(ret == 0 ? "IPv4 set\n" : "failed\n");
		}

	} else if (strcmp(cmd, "get-ipv4") == 0) {
		uint32_t addr, mask, gw;
		ret = rtdash_get_ipv4(&ctx, &addr, &mask, &gw);
		if (ret == 0) {
			struct in_addr a, m, g;
			a.s_addr = addr;
			m.s_addr = mask;
			g.s_addr = gw;
			printf("%s %s %s\n", inet_ntoa(a), inet_ntoa(m), inet_ntoa(g));
		} else {
			printf("failed\n");
		}

	} else if (strcmp(cmd, "recv") == 0) {
		rtdash_arm_req(&ctx);
		rtdash_arm_ack(&ctx);
		struct rtdash_msg msg;
		ret = rtdash_recv(&ctx, &msg);
		if (ret == 0) {
			printf("type=0x%02x len=%u data=", msg.type, msg.len);
			for (uint16_t i = 0; i < msg.len; i++)
				printf("%02x", msg.data[i]);
			printf("\n");
		} else {
			printf("no data\n");
		}
		rtdash_disarm_req(&ctx);
		rtdash_disarm_ack(&ctx);

	} else {
		fprintf(stderr, "unknown command: %s\n", cmd);
		ret = 1;
	}

	rtdash_close(&ctx);
	return ret;
}
