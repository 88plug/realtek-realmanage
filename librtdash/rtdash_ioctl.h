#ifndef RTDASH_IOCTL_H
#define RTDASH_IOCTL_H

/*
 * Kernel-side structs and constants for Realtek DASH driver ioctl interface.
 * Mirrors r8125_dash.h / r8126_dash.h / r8127_dash.h from OpenWRT drivers.
 */

#include <linux/types.h>
#include <linux/sockios.h>

#define SIOCRTLTOOL             (SIOCDEVPRIVATE + 1)  /* 0x89F1 */
#define SIOCDEVPRIVATE_RTLDASH  (SIOCDEVPRIVATE + 2)  /* 0x89F2 */

/* rtltool commands (sent via SIOCRTLTOOL) */
enum rtltool_cmd {
	RTL_READ_MAC            = 0,
	RTL_WRITE_MAC           = 1,
	RTL_READ_PHY            = 2,
	RTL_WRITE_PHY           = 3,
	RTL_READ_EPHY           = 4,
	RTL_WRITE_EPHY          = 5,
	RTL_READ_ERI            = 6,
	RTL_WRITE_ERI           = 7,
	RTL_READ_PCI            = 8,
	RTL_WRITE_PCI           = 9,
	RTL_READ_EEPROM         = 10,
	RTL_WRITE_EEPROM        = 11,
	RTL_READ_OCP            = 12,
	RTL_WRITE_OCP           = 13,
	RTL_ENABLE_PCI_DIAG     = 14,
	RTL_DISABLE_PCI_DIAG    = 15,
};

struct rtltool_cmd_struct {
	__u32 cmd;
	__u32 offset;
	__u32 len;
	__u32 data;
};

/* DASH ioctl commands (sent via SIOCDEVPRIVATE_RTLDASH) */
enum rtl_dash_cmd {
	RTL_DASH_ARP_NS_OFFLOAD                        = 0,
	RTL_DASH_SET_OOB_IPMAC                         = 1,
	RTL_DASH_NOTIFY_OOB                             = 2,
	RTL_DASH_SEND_BUFFER_DATA_TO_DASH_FW            = 3,
	RTL_DASH_CHECK_SEND_BUFFER_TO_DASH_FW_COMPLETE  = 4,
	RTL_DASH_GET_RCV_FROM_FW_BUFFER_DATA            = 5,
	RTL_DASH_OOB_REQ                                = 6,
	RTL_DASH_OOB_ACK                                = 7,
	RTL_DASH_DETACH_OOB_REQ                         = 8,
	RTL_DASH_DETACH_OOB_ACK                         = 9,
	RTL_FW_SET_IPV4                                 = 0x10,
	RTL_FW_GET_IPV4                                 = 0x11,
	RTL_FW_SET_IPV6                                 = 0x12,
	RTL_FW_GET_IPV6                                 = 0x13,
	RTL_FW_SET_EXT_SNMP                             = 0x14,
	RTL_FW_GET_EXT_SNMP                             = 0x15,
	RTL_FW_SET_WAKEUP_PATTERN                       = 0x16,
	RTL_FW_GET_WAKEUP_PATTERN                       = 0x17,
	RTL_FW_DEL_WAKEUP_PATTERN                       = 0x18,
	RTLT_DASH_COMMAND_INVALID                       = 0x19,
};

struct rtl_dash_ioctl_struct {
	__u32 cmd;
	__u32 offset;
	__u32 len;
	union {
		__u32 data;
		void *data_buffer;
	};
};

/* OOB header (8 bytes, precedes every IPC2/CMAC message) */
#define DASH_OOB_HDR_TYPE_REQ  0x91
#define DASH_OOB_HDR_TYPE_ACK  0x92

struct osoob_hdr {
	__le32 len;
	__u8   type;
	__u8   flag;
	__u8   host_req_v;
	__u8   res;
} __attribute__((packed));

/* OOB request types */
#define DASH_OOB_WSMANREG   0x01
#define DASH_OOB_OSPUSHDATA 0x02

/* IPC2 software interrupt commands (via IB2SOC) */
#define IPC2_SWISR_DRIVER_READY             0x05
#define IPC2_SWISR_DRIVER_EXIT              0x06
#define IPC2_SWISR_CLIENTTOOL_SYNC_HOSTNAME 0x20
#define IPC2_SWISR_DIS_DASH                 0x55
#define IPC2_SWISR_EN_DASH                  0x56

/* IPC2 register offsets */
#define IPC2_PCIE_BASE     0xC100
#define IPC2_TX_SET_REG    0xC100
#define IPC2_TX_STATUS_REG 0xC104
#define IPC2_RX_STATUS_REG 0xC108
#define IPC2_RX_CLEAR_REG  0xC10C

/* IPC2 shared memory */
#define IPC2_TX_BUFFER     0x32000
#define IPC2_RX_BUFFER     0x33000
#define IPC2_BUFFER_LENGTH 0x1000

/* IPC2 handshake bits */
#define IPC2_TX_SEND_BIT   (1 << 0)
#define IPC2_TX_ACK_BIT    (1 << 8)
#define IPC2_RX_ROK_BIT    (1 << 0)
#define IPC2_RX_ACK_BIT    (1 << 8)

/* Buffer sizes */
#define RECV_FROM_FW_BUF_SIZE 1520
#define SEND_TO_FW_BUF_SIZE   1520

#endif /* RTDASH_IOCTL_H */
