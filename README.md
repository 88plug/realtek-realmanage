# realtek-realmanage

Native Linux toolkit for Realtek RealManage (DMTF DASH) out-of-band management.

The first open-source Linux management suite for Realtek DASH NICs. Ports the functionality of the Windows-only Realtek Management Console, DASHConfigRT, and RtDashService to Linux.

## What is RealManage?

Realtek RealManage is an out-of-band management engine embedded in certain Realtek Ethernet controllers. It implements [DMTF DASH](https://www.dmtf.org/standards/dash) (Desktop and mobile Architecture for System Hardware), providing remote management capabilities that work regardless of host OS state — even when the machine is powered off.

**Capabilities:** Remote power control, KVM (VNC), Serial-over-LAN, USB/ISO redirect, boot order control, hardware inventory, BIOS management, firmware update, event alerts, account management — all via the NIC's independent management processor.

**Think of it as:** Intel AMT / iLO / IPMI, but built into the Ethernet NIC instead of the chipset. No vPro required. Works with AMD and Intel systems.

## Components

| Tool | Replaces (Windows) | Description |
|------|-------------------|-------------|
| **realmanage** | Realtek Management Console | CLI for remote DASH management (power, KVM, inventory, boot, accounts) |
| **dash-activate** | DASHConfigRT | Provisioning/activation from Linux |
| **rtdashd** | RtDashService | Systemd daemon — OS agent that pushes hostname/OS info to firmware |
| **rtdash-ctl** | — | Low-level ioctl tool for direct firmware communication |
| **librtdash** | — | C library for the Realtek DASH driver ioctl interface |

## Supported Hardware

DASH-capable Realtek NICs only (not regular consumer chips):

| Chip | Speed | DASH Type | IPC | Status |
|------|-------|-----------|-----|--------|
| RTL8111DP | 1 GbE | Type 1 | Basic OOB | Untested |
| RTL8111EP | 1 GbE | Type 2 | CMAC | Should work |
| RTL8111FP | 1 GbE | Type 3 | CMAC | Should work |
| **RTL8125BP** | **2.5 GbE** | **Type 4** | **IPC2** | **Primary target** |
| RTL8126 | 5 GbE | Type 2/3 | CMAC | Should work |
| **RTL8127AP** | **10 GbE** | **Type 4** | **IPC2** | **Should work** |

**NOT supported** (no management engine): RTL8111B/C/D/E/F/G/H/K/L, RTL8125B (consumer).

### Platform Compatibility

| Vendor | Models | Notes |
|--------|--------|-------|
| HP | EliteDesk 805, ProDesk 405, EliteBook 845 | AMD PRO platforms, RTL8111FPH |
| Lenovo | ThinkCentre M75, ThinkStation P620 | AMD PRO platforms |
| Gigabyte | B550M, X570 with DASH NIC | RTL8111EP confirmed |
| Dell | OptiPlex 7000 AMD, Precision 3660 AMD | RTL8111EP/FP |
| DIY | Any board with DASH-capable Realtek NIC | Check BIOS for RealManage option |

## Quick Start

### Prerequisites

- DASH-capable Realtek NIC (see table above)
- DASH enabled in BIOS (Advanced → Network → RealManage Firmware Control)
- Realtek out-of-tree driver with DASH support (`r8125`, `r8126`, or `r8127`)
- `wsmancli` package for remote management

### Install

```bash
# From source
make
sudo make install

# Enable the OS agent daemon
sudo systemctl enable --now rtdashd

# AUR (Arch Linux)
yay -S realtek-realmanage

# Also install AMD DASH CLI for additional capabilities
yay -S amd-dash-cli-bin
```

### Activate DASH

```bash
# Auto-detect NIC, generate strong password
sudo dash-activate

# Or specify everything
sudo dash-activate -i enp3s0 -u admin -p 'MyP@ss123' -4 192.168.1.50/255.255.255.0/192.168.1.1
```

### Manage Remotely

```bash
# System info
realmanage -t 192.168.1.50 info

# Power control
realmanage -t 192.168.1.50 power status
realmanage -t 192.168.1.50 power on
realmanage -t 192.168.1.50 power off-graceful
realmanage -t 192.168.1.50 power cycle

# Hardware inventory
realmanage -t 192.168.1.50 cpu
realmanage -t 192.168.1.50 memory
realmanage -t 192.168.1.50 bios
realmanage -t 192.168.1.50 sensors

# KVM remote desktop
realmanage -t 192.168.1.50 kvm

# Boot control
realmanage -t 192.168.1.50 boot
realmanage -t 192.168.1.50 boot set PXE

# Discover DASH systems on network
realmanage discover 192.168.1.0/24

# Raw WS-Man for advanced use
realmanage -t 192.168.1.50 raw http://schemas.dmtf.org/wbem/wscim/1/cim-schema/2/CIM_Processor
```

### Low-Level Tools

```bash
# Check if NIC supports DASH
sudo rtdash-ctl -i enp3s0 check

# Query OOB IP configuration
sudo rtdash-ctl -i enp3s0 get-ipv4

# Set OOB IP
sudo rtdash-ctl -i enp3s0 set-ipv4 192.168.1.50 255.255.255.0 192.168.1.1

# Send driver ready signal
sudo rtdash-ctl -i enp3s0 driver-ready

# Sync hostname to firmware
sudo rtdash-ctl -i enp3s0 sync-hostname
```

## How It Works

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Remote Management Host                    │
│  realmanage / AMD DASH CLI / browser (http://<oob-ip>:623)  │
└──────────────────────────┬──────────────────────────────────┘
                           │ WS-Man / SOAP / HTTP(S)
                           │ Port 623 (HTTP) / 664 (HTTPS)
┌──────────────────────────┴──────────────────────────────────┐
│                    Realtek NIC (DASH)                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  DASH Management Engine (runs on NIC's embedded CPU)    │ │
│  │  - WS-Man server on port 623/664                        │ │
│  │  - Embedded web server                                  │ │
│  │  - Power control, KVM, SOL, IDER                        │ │
│  │  - Independent of host OS                               │ │
│  └────────────────────────┬────────────────────────────────┘ │
│                           │ IPC2 mailbox (MMIO 0x32000-0x33000)
│                           │ or CMAC (MMIO 0xBAF20000)       │
└──────────────────────────┬──────────────────────────────────┘
                           │ ioctl SIOCDEVPRIVATE_RTLDASH (0x89F2)
┌──────────────────────────┴──────────────────────────────────┐
│                      Host OS (Linux)                          │
│  rtdashd (OS agent) → pushes hostname, OS info, heartbeat   │
│  dash-activate      → initial provisioning via ioctl         │
│  rtdash-ctl         → low-level firmware communication       │
└─────────────────────────────────────────────────────────────┘
```

### Protocol Stack

```
Application:  DMTF DASH CIM Profiles
Binding:      WS-Management CIM Binding (DSP0227)
Transport:    WS-Management / SOAP 1.2 / XML
Security:     HTTP Digest (Class A) or TLS + Digest (Class B)
Network:      HTTP port 623 / HTTPS port 664
```

### vs Intel AMT

| | RealManage | Intel AMT |
|---|---|---|
| **Location** | In the NIC | In the PCH/chipset (ME) |
| **CPU requirement** | Any (AMD or Intel) | Intel vPro only |
| **Standard** | Open DMTF DASH | Proprietary |
| **Provisioning** | BIOS + DASHConfigRT | Certificate-based, zero-touch |
| **KVM** | VNC over SSH | RFB (proprietary) |
| **Cost** | No additional licensing | vPro premium |

## Driver Requirements

The mainline `r8169` kernel driver has basic DASH awareness but does **not** expose the management ioctl interface. You need Realtek's out-of-tree drivers:

- **r8125** (2.5 GbE): [github.com/openwrt/rtl8125](https://github.com/openwrt/rtl8125)
- **r8126** (5 GbE): [github.com/openwrt/rtl8126](https://github.com/openwrt/rtl8126)
- **r8127** (10 GbE): Available from Realtek

The driver must be compiled with `ENABLE_DASH_SUPPORT=y` in the Makefile.

## Related Projects

- [88plug/intel-amt-linux](https://github.com/88plug/intel-amt-linux) — Inspiration for this project. Linux AMT management.
- [88plug/amt-activate-linux](https://github.com/88plug/amt-activate-linux) — AMT activation from Linux.
- [AMD DASH CLI](https://www.amd.com/en/support/downloads/manageability-tools.html) — AMD's official DASH CLI (closed-source, Linux .deb/.rpm available).
- [Openwsman/wsmancli](https://github.com/Openwsman/wsmancli) — WS-Management CLI (the transport layer).

## License

[FSL-1.1-ALv2](LICENSE.md) — Functional Source License with Apache 2.0 future license.
