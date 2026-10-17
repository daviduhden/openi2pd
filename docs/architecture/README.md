# Architecture decisions

This directory records cross-component decisions for the Open I2Pd fork.

## ADR-001 — Monolithic runtime with an isolated logging worker (interim)

- **Status:** Accepted as an intermediate step; full multiprocess split is
  deferred by project instruction.
- **Context:** I2P session state, tunnel crypto, netDb persistence and the event
  loop are coupled to process-wide singletons. Splitting a state machine
  without clear ownership risks nonce reuse, lost retransmissions and
  inconsistent tunnel state.
- **Decision:** Keep the router runtime monolithic for now, but extract the
  logging writer into an exec'ed worker over a versioned
  `AF_UNIX/SOCK_SEQPACKET` channel. The core never opens the configured log
  file when `privsep.enabled=true`.
- **Consequences:** Real reduction of one capability (log file write) from the
  core, plus reusable IPC/worker infrastructure. The core still combines
  network, persistence and long-term keys; this is a documented risk.
- **Follow-up:** `PRIVSEP-005..014` in `TODO.md`.

## ADR-002 — IPC framing and authentication

- **Status:** Implemented for the log channel.
- **Decision:** Binary, network-byte-order frames with magic, version, type,
  request ID and payload length. Payload is field-encoded, never raw C++
  layouts. `SOCK_SEQPACKET` preserves message boundaries. Peer UID is checked
  with `SO_PEERCRED`/`getpeereid`; channel fds are `CLOEXEC`; worker processes
  are exec'ed and close inherited descriptors.
- **Limits:** 64 KiB frame, 8 KiB text, poll timeouts. No generic filesystem
  operations and no descriptor passing yet.
- **Consequences:** The protocol can be reused for future workers; each new
  worker needs its own operation allowlist and OS policy.

## ADR-003 — Android identity and toolchain

- **Status:** Implemented / build validation blocked on Android SDK.
- **Decision:** Application ID/namespace `org.openi2pd.router`, UI name
  `Open I2Pd`, app-specific storage, no WebView console, no broad storage
  permission. Build with OpenJDK 25, Gradle 9.8.1, AGP 9.4.1, compile/target
  SDK 37, minSdk 24, Java source level 17 (D8/R8 limit), NDK r27+.
- **Consequences:** Existing upstream app cannot update in place; migration is
  documented in `docs/REBRANDING.md` and `TODO.md`.

## ADR-004 — CMake/POSIX make and test layout

- **Status:** Implemented.
- **Decision:** CMake is the single rule source; the root `Makefile` is a POSIX
  front-end using Ninja (CMake's Unix Makefiles generator emits GNU make
  syntax). Tests are CTest targets; tools and TUI are optional targets.
- **Consequences:** GNU make is not required by maintained build files.
  OpenBSD uses `bmake`/Ninja; packaging invokes CMake directly.

## ADR-005 — Repository identity and provenance

- **Status:** Implemented; executable/config path rename deferred.
- **Decision:** Use the existing Open I2Pd identity. Preserve copyright and
  upstream provenance. Rename Android app identity and user-visible branding;
  keep `i2pd` binary, library names and persisted paths until a dual-read
  migration and compatibility launcher exist.
- **Consequences:** Coherent user-visible identity without breaking I2P wire
  formats, packages or existing data. See `docs/REBRANDING.md`.

## Runtime diagram (current)

```
                 ┌──────────────────────────────┐
                 │ i2pd core process            │
                 │ config, keys, netDb, tunnels │
                 │ NTCP2/SSU2 sockets, router   │
                 │ logic, TUI server thread     │
                 └──────────────┬───────────────┘
                                │ AF_UNIX/SOCK_SEQPACKET
                                │ versioned log frames
                 ┌──────────────▼───────────────┐
                 │ log worker process           │
                 │ file/syslog only             │
                 │ no inet/dns (OpenBSD pledge) │
                 └──────────────────────────────┘

  i2pd-tui ── local 0600 UNIX socket ──> TUI server thread (read-only)
```

Target process topology and capability matrices are in
`docs/security/privilege-separation.md` and
`docs/security/process-capabilities.md`.
