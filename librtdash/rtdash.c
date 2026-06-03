#include "rtdash.h"
#include "rtdash_ioctl.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>

static int rtdash_rtltool_cmd(struct rtdash_ctx *ctx, uint32_t cmd,
                              uint32_t offset, uint32_t len, uint32_t data)
{
	struct ifreq ifr;
	struct rtltool_cmd_struct tool = {
		.cmd    = cmd,
		.offset = offset,
		.len    = len,
		.data   = data,
	};

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ctx->ifname, IFNAMSIZ - 1);
	ifr.ifr_data = (void *)&tool;

	return ioctl(ctx->sock, SIOCRTLTOOL, &ifr);
}

static int rtdash_dash_cmd(struct rtdash_ctx *ctx, uint32_t cmd,
                           uint32_t offset, uint32_t len, void *buf)
{
	struct ifreq ifr;
	struct rtl_dash_ioctl_struct dash = {
		.cmd    = cmd,
		.offset = offset,
		.len    = len,
	};
	dash.data_buffer = buf;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ctx->ifname, IFNAMSIZ - 1);
	ifr.ifr_data = (void *)&dash;

	return ioctl(ctx->sock, SIOCDEVPRIVATE_RTLDASH, &ifr);
}

static int rtdash_dash_cmd_simple(struct rtdash_ctx *ctx, uint32_t cmd)
{
	return rtdash_dash_cmd(ctx, cmd, 0, 0, NULL);
}

int rtdash_open(struct rtdash_ctx *ctx, const char *ifname)
{
	memset(ctx, 0, sizeof(*ctx));
	strncpy(ctx->ifname, ifname, sizeof(ctx->ifname) - 1);

	ctx->sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (ctx->sock < 0)
		return -errno;

	return 0;
}

void rtdash_close(struct rtdash_ctx *ctx)
{
	if (ctx->ack_armed)
		rtdash_disarm_ack(ctx);
	if (ctx->req_armed)
		rtdash_disarm_req(ctx);
	if (ctx->diag_enabled)
		rtdash_disable_diag(ctx);
	if (ctx->sock >= 0)
		close(ctx->sock);
	ctx->sock = -1;
}

int rtdash_enable_diag(struct rtdash_ctx *ctx)
{
	int ret = rtdash_rtltool_cmd(ctx, RTL_ENABLE_PCI_DIAG, 0, 0, 0);
	if (ret == 0)
		ctx->diag_enabled = true;
	return ret;
}

int rtdash_disable_diag(struct rtdash_ctx *ctx)
{
	int ret = rtdash_rtltool_cmd(ctx, RTL_DISABLE_PCI_DIAG, 0, 0, 0);
	if (ret == 0)
		ctx->diag_enabled = false;
	return ret;
}

bool rtdash_is_dash_capable(struct rtdash_ctx *ctx)
{
	return rtdash_dash_cmd_simple(ctx, RTL_DASH_OOB_REQ) == 0;
}

int rtdash_arm_req(struct rtdash_ctx *ctx)
{
	int ret = rtdash_dash_cmd_simple(ctx, RTL_DASH_OOB_REQ);
	if (ret == 0)
		ctx->req_armed = true;
	return ret;
}

int rtdash_arm_ack(struct rtdash_ctx *ctx)
{
	int ret = rtdash_dash_cmd_simple(ctx, RTL_DASH_OOB_ACK);
	if (ret == 0)
		ctx->ack_armed = true;
	return ret;
}

int rtdash_disarm_req(struct rtdash_ctx *ctx)
{
	int ret = rtdash_dash_cmd_simple(ctx, RTL_DASH_DETACH_OOB_REQ);
	if (ret == 0)
		ctx->req_armed = false;
	return ret;
}

int rtdash_disarm_ack(struct rtdash_ctx *ctx)
{
	int ret = rtdash_dash_cmd_simple(ctx, RTL_DASH_DETACH_OOB_ACK);
	if (ret == 0)
		ctx->ack_armed = false;
	return ret;
}

int rtdash_send(struct rtdash_ctx *ctx, const void *data, uint16_t len, uint8_t type)
{
	uint8_t buf[4 + sizeof(struct osoob_hdr) + RTDASH_MAX_PAYLOAD];
	uint16_t total = sizeof(struct osoob_hdr) + len;
	struct osoob_hdr hdr = {
		.len        = htole32(len),
		.type       = DASH_OOB_OSPUSHDATA,
		.flag       = 0,
		.host_req_v = type,
		.res        = 0,
	};

	if (len > RTDASH_MAX_PAYLOAD)
		return -EINVAL;

	memset(buf, 0, sizeof(buf));
	memcpy(buf, &total, 2);
	memcpy(buf + 4, &hdr, sizeof(hdr));
	if (len > 0 && data)
		memcpy(buf + 4 + sizeof(hdr), data, len);

	return rtdash_dash_cmd(ctx, RTL_DASH_SEND_BUFFER_DATA_TO_DASH_FW,
	                       0, 4 + total, buf);
}

int rtdash_send_complete(struct rtdash_ctx *ctx)
{
	uint8_t buf[4];
	int ret = rtdash_dash_cmd(ctx, RTL_DASH_CHECK_SEND_BUFFER_TO_DASH_FW_COMPLETE,
	                          0, sizeof(buf), buf);
	if (ret < 0)
		return ret;

	uint16_t sent;
	memcpy(&sent, buf, 2);
	if (sent == 0xFFFF)
		return -EAGAIN;
	return sent;
}

int rtdash_recv(struct rtdash_ctx *ctx, struct rtdash_msg *msg)
{
	uint8_t buf[2 + RECV_FROM_FW_BUF_SIZE + 2];
	int ret;

	memset(buf, 0, sizeof(buf));
	ret = rtdash_dash_cmd(ctx, RTL_DASH_GET_RCV_FROM_FW_BUFFER_DATA,
	                      0, sizeof(buf), buf);
	if (ret < 0)
		return ret;

	uint16_t recv_len;
	memcpy(&recv_len, buf, 2);
	if (recv_len == 0)
		return -EAGAIN;

	struct osoob_hdr *hdr = (struct osoob_hdr *)(buf + 2);
	uint16_t payload_len = le32toh(hdr->len);

	if (payload_len > RTDASH_MAX_PAYLOAD)
		payload_len = RTDASH_MAX_PAYLOAD;

	msg->len = payload_len;
	memcpy(&msg->type, buf + 2 + recv_len, 1);
	if (payload_len > 0)
		memcpy(msg->data, buf + 2 + sizeof(struct osoob_hdr), payload_len);

	return 0;
}

int rtdash_sw_interrupt(struct rtdash_ctx *ctx, uint32_t cmd)
{
	return rtdash_dash_cmd(ctx, RTL_DASH_NOTIFY_OOB, 0, 4, &cmd);
}

int rtdash_driver_ready(struct rtdash_ctx *ctx)
{
	return rtdash_sw_interrupt(ctx, IPC2_SWISR_DRIVER_READY);
}

int rtdash_driver_exit(struct rtdash_ctx *ctx)
{
	return rtdash_sw_interrupt(ctx, IPC2_SWISR_DRIVER_EXIT);
}

int rtdash_sync_hostname(struct rtdash_ctx *ctx)
{
	return rtdash_sw_interrupt(ctx, IPC2_SWISR_CLIENTTOOL_SYNC_HOSTNAME);
}

int rtdash_enable_dash(struct rtdash_ctx *ctx)
{
	return rtdash_sw_interrupt(ctx, IPC2_SWISR_EN_DASH);
}

int rtdash_disable_dash(struct rtdash_ctx *ctx)
{
	return rtdash_sw_interrupt(ctx, IPC2_SWISR_DIS_DASH);
}

int rtdash_set_oob_ipmac(struct rtdash_ctx *ctx, uint32_t ip, const uint8_t *mac)
{
	uint8_t buf[10];
	memcpy(buf, &ip, 4);
	memcpy(buf + 4, mac, 6);
	return rtdash_dash_cmd(ctx, RTL_DASH_SET_OOB_IPMAC, 0, 10, buf);
}

int rtdash_set_ipv4(struct rtdash_ctx *ctx, uint32_t addr, uint32_t mask, uint32_t gw)
{
	uint32_t buf[3] = { addr, mask, gw };
	return rtdash_dash_cmd(ctx, RTL_FW_SET_IPV4, 0, 12, buf);
}

int rtdash_get_ipv4(struct rtdash_ctx *ctx, uint32_t *addr, uint32_t *mask, uint32_t *gw)
{
	uint32_t buf[3] = {0};
	int ret = rtdash_dash_cmd(ctx, RTL_FW_GET_IPV4, 0, 12, buf);
	if (ret == 0) {
		*addr = buf[0];
		*mask = buf[1];
		*gw   = buf[2];
	}
	return ret;
}

int rtdash_push_os_data(struct rtdash_ctx *ctx, const char *hostname,
                        const char *os_name, const char *os_version)
{
	uint8_t payload[512];
	int off = 0;

	if (hostname) {
		size_t hlen = strlen(hostname);
		if (hlen > 128) hlen = 128;
		payload[off++] = 0x01;
		payload[off++] = (uint8_t)hlen;
		memcpy(payload + off, hostname, hlen);
		off += hlen;
	}

	if (os_name) {
		size_t nlen = strlen(os_name);
		if (nlen > 128) nlen = 128;
		payload[off++] = 0x02;
		payload[off++] = (uint8_t)nlen;
		memcpy(payload + off, os_name, nlen);
		off += nlen;
	}

	if (os_version) {
		size_t vlen = strlen(os_version);
		if (vlen > 128) vlen = 128;
		payload[off++] = 0x03;
		payload[off++] = (uint8_t)vlen;
		memcpy(payload + off, os_version, vlen);
		off += vlen;
	}

	return rtdash_send(ctx, payload, off, DASH_OOB_HDR_TYPE_ACK);
}
