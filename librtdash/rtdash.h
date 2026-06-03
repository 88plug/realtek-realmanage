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

/* OS data push (hostname, OS info) */
int rtdash_push_os_data(struct rtdash_ctx *ctx, const char *hostname,
                        const char *os_name, const char *os_version);

#endif /* RTDASH_H */
