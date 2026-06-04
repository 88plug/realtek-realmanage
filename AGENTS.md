# AGENTS.md — AI Agent Instructions for realtek-realmanage

## What this repo is

Native Linux toolkit for Realtek RealManage (DMTF DASH) out-of-band management.
Ports three Windows-only tools to Linux:
- `realmanage` (CLI) ← Realtek Management Console
- `dash-activate` (script) ← DASHConfigRT
- `rtdashd` (daemon) ← RtDashService
- `rtdash-ctl` (CLI) ← no Windows equivalent
- `librtdash` (C library) ← no Windows equivalent

## Build

```bash
make                    # builds all targets
make install PREFIX=/usr  # install
make check              # bash -n on scripts
make clean
```

Docker verify (canonical):
```bash
docker build -t rtm-verify -f - . <<'DOCKERFILE'
FROM ubuntu:latest
RUN apt-get update -qq && apt-get install -y gcc make libc6-dev shellcheck cppcheck
WORKDIR /app
COPY . .
CMD ["make", "-j4"]
DOCKERFILE
docker run --rm rtm-verify
```

## Architecture

```
librtdash/rtdash.h          Public C API
librtdash/rtdash_ioctl.h    Kernel ioctl constants (SIOCDEVPRIVATE_RTLDASH)
librtdash/rtdash.c          Library implementation
librtdash/rtdash-ctl.c      Low-level CLI tool
rtdashd/rtdashd.c           Systemd OS agent daemon
realmanage-cli/realmanage   Bash WS-Man management CLI (~1150 lines)
dash-activate/dash-activate Bash provisioning script (~500 lines)
```

## Key technical facts

- **ioctl**: `SIOCDEVPRIVATE_RTLDASH` (0x89F2) for DASH commands; `SIOCRTLTOOL` (0x89F1) for diag enable + OCP reads
- **IPC2**: TX MMIO 0x32000, RX 0x33000, control 0xC100 — used by RTL8125BP and RTL8127AP (DASH Type 4)
- **CMAC**: DMA descriptors at 0xBAF20000 — used by RTL8111EP/FP, RTL8126 (DASH Type 2/3)
- **Protocol**: WS-Man/SOAP over HTTP port 623, HTTPS port 664
- **Requires root** and out-of-tree driver with `ENABLE_DASH_SUPPORT=y`
- **C standard**: `-std=c11`, `_Atomic` for thread-shared vars, `_GNU_SOURCE` for epoll/timerfd
- **Bash**: `set -euo pipefail` in both scripts, shellcheck-clean

## Code conventions

- C: `-Wall -Wextra`, zero warnings on GCC 15 (ubuntu:latest Docker)
- No `strncpy(ifr.ifr_name, ...)` — use `memcpy(ifr.ifr_name, ctx->ifname, IFNAMSIZ)`
- `ssize_t _r = write(...); (void)_r;` pattern for intentionally-ignored return values
- Use `_Atomic` for any int shared between threads
- `opendir()` not `popen("ls ...")` for directory iteration
- ABI mirroring of kernel ioctl structs is intentional — same pattern as every ioctl-using userspace tool

## What NOT to change

- Do not add external C library dependencies beyond glibc
- Do not change `realmanage` or `dash-activate` to require Python or Node
- Do not break backward compat of `librtdash.h` public API without bumping major version
- Do not add `Co-Authored-By: Claude` or AI attribution to commits (house policy)

## Testing without hardware

The ioctl calls will return -EOPNOTSUPP on any system without a DASH-capable Realtek NIC.
All other code (WS-Man, bash scripts, config parsing, Prometheus endpoint) can be tested.
The bash scripts' JSON output and argument parsing can be tested with `-n` (dry run where applicable).

## Related

- See `llms.txt` for full documentation index
- See `CLAUDE.md` for project-specific build/safety context
