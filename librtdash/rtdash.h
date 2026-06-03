#ifndef RTDASH_H
#define RTDASH_H

#include <stdint.h>
#include <stdbool.h>

#define RTDASH_MAX_PAYLOAD 1512  /* 1520 - 8 byte OOB header */

struct rtdash_ctx {
	int sock;
	char ifname[16];
	bool diag_enabled;
	bool req_armed;
	bool ack_armed;
};

struct rtdash_msg {
	uint8_t  type;     /* DASH_OOB_HDR_TYPE_REQ or _ACK */
	uint16_t len;
	uint8_t  data[RTDASH_MAX_PAYLOAD];
};

/* Lifecycle */
int  rtdash_open(struct rtdash_ctx *ctx, const char *ifname);
void rtdash_close(struct rtdash_ctx *ctx);

/* Diagnostics gate (required before any DASH ioctl) */
int rtdash_enable_diag(struct rtdash_ctx *ctx);
int rtdash_disable_diag(struct rtdash_ctx *ctx);

/* DASH detection */
bool rtdash_is_dash_capable(struct rtdash_ctx *ctx);
uint32_t rtdash_get_fw_version(struct rtdash_ctx *ctx);

/* OOB listener management */
int rtdash_arm_req(struct rtdash_ctx *ctx);
int rtdash_arm_ack(struct rtdash_ctx *ctx);
int rtdash_disarm_req(struct rtdash_ctx *ctx);
int rtdash_disarm_ack(struct rtdash_ctx *ctx);

/* TX: send OOB message to DASH firmware */
int rtdash_send(struct rtdash_ctx *ctx, const void *data, uint16_t len, uint8_t type);
int rtdash_send_complete(struct rtdash_ctx *ctx);

/* RX: receive OOB message from DASH firmware */
int rtdash_recv(struct rtdash_ctx *ctx, struct rtdash_msg *msg);

/* IPC2 software interrupts (driver ready, hostname sync, etc.) */
int rtdash_sw_interrupt(struct rtdash_ctx *ctx, uint32_t cmd);
int rtdash_driver_ready(struct rtdash_ctx *ctx);
int rtdash_driver_exit(struct rtdash_ctx *ctx);
int rtdash_sync_hostname(struct rtdash_ctx *ctx);
int rtdash_enable_dash(struct rtdash_ctx *ctx);
int rtdash_disable_dash(struct rtdash_ctx *ctx);

/* Network configuration */
int rtdash_set_oob_ipmac(struct rtdash_ctx *ctx, uint32_t ip, const uint8_t *mac);
int rtdash_set_ipv4(struct rtdash_ctx *ctx, uint32_t addr, uint32_t mask, uint32_t gw);
int rtdash_get_ipv4(struct rtdash_ctx *ctx, uint32_t *addr, uint32_t *mask, uint32_t *gw);
int rtdash_set_ipv6(struct rtdash_ctx *ctx, const uint8_t addr[16], uint8_t prefix, const uint8_t gw[16]);
int rtdash_get_ipv6(struct rtdash_ctx *ctx, uint8_t addr[16], uint8_t *prefix, uint8_t gw[16]);

/* SNMP configuration */
struct rtdash_snmp_config {
	uint32_t trap_ip;
	uint16_t trap_port;
	char     community[32];
	uint8_t  enabled;
};
int rtdash_set_snmp(struct rtdash_ctx *ctx, const struct rtdash_snmp_config *cfg);
int rtdash_get_snmp(struct rtdash_ctx *ctx, struct rtdash_snmp_config *cfg);

/* Wake-on-LAN patterns */
struct rtdash_wake_pattern {
	uint8_t mask[16];
	uint8_t pattern[128];
	uint8_t len;
	uint8_t id;
};
int rtdash_set_wake_pattern(struct rtdash_ctx *ctx, const struct rtdash_wake_pattern *p);
int rtdash_get_wake_pattern(struct rtdash_ctx *ctx, uint8_t id, struct rtdash_wake_pattern *p);
int rtdash_del_wake_pattern(struct rtdash_ctx *ctx, uint8_t id);

/* ARP/NS offload */
struct rtdash_arp_offload {
	uint32_t ipv4;
	uint8_t  ipv6[16];
	uint8_t  mac[6];
	uint8_t  enabled;
};
int rtdash_set_arp_offload(struct rtdash_ctx *ctx, const struct rtdash_arp_offload *cfg);

/* OS data push (hostname, OS info) */
int rtdash_push_os_data(struct rtdash_ctx *ctx, const char *hostname,
                        const char *os_name, const char *os_version);

#endif /* RTDASH_H */
