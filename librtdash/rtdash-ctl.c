#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "rtdash.h"
#include "rtdash_ioctl.h"

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s -i <interface> <command> [args...]\n", prog);
	fprintf(stderr, "\nCommands:\n");
	fprintf(stderr, "  check                          Check if interface supports DASH\n");
	fprintf(stderr, "  dash-version                   Get DASH firmware version\n");
	fprintf(stderr, "  mac-get                        Get NIC MAC address\n");
	fprintf(stderr, "  driver-ready                   Send DRIVER_READY signal\n");
	fprintf(stderr, "  driver-exit                    Send DRIVER_EXIT signal\n");
	fprintf(stderr, "  sync-hostname                  Sync hostname to firmware\n");
	fprintf(stderr, "  enable-dash                    Enable DASH\n");
	fprintf(stderr, "  disable-dash                   Disable DASH\n");
	fprintf(stderr, "  get-ipv4                       Get OOB IPv4 configuration\n");
	fprintf(stderr, "  set-ipv4 <ip> <mask> <gw>      Set OOB IPv4 configuration\n");
	fprintf(stderr, "  get-ipv6                       Get OOB IPv6 configuration\n");
	fprintf(stderr, "  set-ipv6 <addr> <prefix> <gw>  Set OOB IPv6 configuration\n");
	fprintf(stderr, "  snmp-get                       Get SNMP configuration\n");
	fprintf(stderr, "  snmp-set <trap-ip> <port> <community>  Set SNMP configuration\n");
	fprintf(stderr, "  wake-pattern-get <id>          Get wake pattern by ID\n");
	fprintf(stderr, "  wake-pattern-set <id> <hex>    Set wake pattern (hex payload)\n");
	fprintf(stderr, "  wake-pattern-del <id>          Delete wake pattern\n");
	fprintf(stderr, "  arp-offload-set <ip4> <ip6> <mac> <0|1>  Configure ARP/NS offload\n");
	fprintf(stderr, "  recv                           Receive OOB message from firmware\n");
}

static int parse_hex(const char *s, uint8_t *out, size_t max_len)
{
	size_t i;
	size_t slen = strlen(s);
	if (slen % 2 != 0 || slen / 2 > max_len)
		return -1;
	for (i = 0; i < slen / 2; i++) {
		char byte[3] = { s[i*2], s[i*2+1], '\0' };
		out[i] = (uint8_t)strtoul(byte, NULL, 16);
	}
	return (int)(slen / 2);
}

int main(int argc, char *argv[])
{
	const char *ifname = NULL;
	int argi = 1;

	if (argc < 3) {
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
		if (rtdash_is_dash_capable(&ctx))
			printf("DASH: capable\n");
		else {
			printf("DASH: not capable\n");
			ret = 1;
		}

	} else if (strcmp(cmd, "dash-version") == 0) {
		uint32_t ver = rtdash_get_fw_version(&ctx);
		if (ver == 0)
			printf("version: unavailable\n");
		else
			printf("version: %u.%u.%u.%u\n",
			       (ver >> 24) & 0xFF, (ver >> 16) & 0xFF,
			       (ver >> 8) & 0xFF, ver & 0xFF);

	} else if (strcmp(cmd, "mac-get") == 0) {
		char path[64], mac[18] = "";
		FILE *fp;
		snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifname);
		fp = fopen(path, "r");
		if (!fp) {
			perror("mac-get");
			ret = 1;
		} else {
			if (fgets(mac, sizeof(mac), fp)) {
				mac[strcspn(mac, "\n")] = '\0';
				printf("%s\n", mac);
			}
			fclose(fp);
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
			a.s_addr = addr; m.s_addr = mask; g.s_addr = gw;
			printf("%s %s %s\n", inet_ntoa(a), inet_ntoa(m), inet_ntoa(g));
		} else {
			printf("failed\n");
		}

	} else if (strcmp(cmd, "set-ipv6") == 0) {
		if (argi + 2 >= argc) {
			fprintf(stderr, "usage: set-ipv6 <addr> <prefix> <gw>\n");
			ret = 1;
		} else {
			uint8_t addr[16], gw[16];
			uint8_t prefix = (uint8_t)atoi(argv[argi + 1]);
			if (inet_pton(AF_INET6, argv[argi], addr) != 1 ||
			    inet_pton(AF_INET6, argv[argi + 2], gw) != 1) {
				fprintf(stderr, "invalid IPv6 address\n");
				ret = 1;
			} else {
				ret = rtdash_set_ipv6(&ctx, addr, prefix, gw);
				printf(ret == 0 ? "IPv6 set\n" : "failed\n");
			}
		}

	} else if (strcmp(cmd, "get-ipv6") == 0) {
		uint8_t addr[16], gw[16], prefix;
		ret = rtdash_get_ipv6(&ctx, addr, &prefix, gw);
		if (ret == 0) {
			char abuf[INET6_ADDRSTRLEN], gbuf[INET6_ADDRSTRLEN];
			inet_ntop(AF_INET6, addr, abuf, sizeof(abuf));
			inet_ntop(AF_INET6, gw, gbuf, sizeof(gbuf));
			printf("%s/%u gw=%s\n", abuf, prefix, gbuf);
		} else {
			printf("failed\n");
		}

	} else if (strcmp(cmd, "snmp-set") == 0) {
		if (argi + 2 >= argc) {
			fprintf(stderr, "usage: snmp-set <trap-ip> <port> <community>\n");
			ret = 1;
		} else {
			struct rtdash_snmp_config cfg = {0};
			cfg.trap_ip   = inet_addr(argv[argi]);
			cfg.trap_port = (uint16_t)atoi(argv[argi + 1]);
			strncpy(cfg.community, argv[argi + 2], sizeof(cfg.community) - 1);
			cfg.enabled = 1;
			ret = rtdash_set_snmp(&ctx, &cfg);
			printf(ret == 0 ? "SNMP configured\n" : "failed\n");
		}

	} else if (strcmp(cmd, "snmp-get") == 0) {
		struct rtdash_snmp_config cfg = {0};
		ret = rtdash_get_snmp(&ctx, &cfg);
		if (ret == 0) {
			struct in_addr a;
			a.s_addr = cfg.trap_ip;
			printf("trap=%s:%u community=%s enabled=%d\n",
			       inet_ntoa(a), cfg.trap_port, cfg.community, cfg.enabled);
		} else {
			printf("failed\n");
		}

	} else if (strcmp(cmd, "wake-pattern-set") == 0) {
		if (argi + 1 >= argc) {
			fprintf(stderr, "usage: wake-pattern-set <id> <pattern-hex>\n");
			ret = 1;
		} else {
			struct rtdash_wake_pattern p = {0};
			p.id = (uint8_t)atoi(argv[argi]);
			int plen = parse_hex(argv[argi + 1], p.pattern, sizeof(p.pattern));
			if (plen < 0) {
				fprintf(stderr, "invalid hex pattern\n");
				ret = 1;
			} else {
				p.len = (uint8_t)plen;
				ret = rtdash_set_wake_pattern(&ctx, &p);
				printf(ret == 0 ? "wake pattern set\n" : "failed\n");
			}
		}

	} else if (strcmp(cmd, "wake-pattern-get") == 0) {
		if (argi >= argc) {
			fprintf(stderr, "usage: wake-pattern-get <id>\n");
			ret = 1;
		} else {
			struct rtdash_wake_pattern p = {0};
			uint8_t id = (uint8_t)atoi(argv[argi]);
			ret = rtdash_get_wake_pattern(&ctx, id, &p);
			if (ret == 0) {
				printf("id=%u len=%u pattern=", p.id, p.len);
				for (int i = 0; i < p.len; i++)
					printf("%02x", p.pattern[i]);
				printf("\n");
			} else {
				printf("failed\n");
			}
		}

	} else if (strcmp(cmd, "wake-pattern-del") == 0) {
		if (argi >= argc) {
			fprintf(stderr, "usage: wake-pattern-del <id>\n");
			ret = 1;
		} else {
			uint8_t id = (uint8_t)atoi(argv[argi]);
			ret = rtdash_del_wake_pattern(&ctx, id);
			printf(ret == 0 ? "wake pattern deleted\n" : "failed\n");
		}

	} else if (strcmp(cmd, "arp-offload-set") == 0) {
		if (argi + 3 >= argc) {
			fprintf(stderr, "usage: arp-offload-set <ipv4> <ipv6> <mac> <0|1>\n");
			ret = 1;
		} else {
			struct rtdash_arp_offload cfg = {0};
			cfg.ipv4    = inet_addr(argv[argi]);
			if (inet_pton(AF_INET6, argv[argi + 1], cfg.ipv6) != 1) {
				fprintf(stderr, "invalid IPv6 address\n");
				ret = 1;
			} else {
				unsigned int m[6];
				if (sscanf(argv[argi + 2], "%x:%x:%x:%x:%x:%x",
				           &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) {
					fprintf(stderr, "invalid MAC address\n");
					ret = 1;
				} else {
					for (int i = 0; i < 6; i++)
						cfg.mac[i] = (uint8_t)m[i];
					cfg.enabled = (uint8_t)atoi(argv[argi + 3]);
					ret = rtdash_set_arp_offload(&ctx, &cfg);
					printf(ret == 0 ? "ARP/NS offload configured\n" : "failed\n");
				}
			}
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
