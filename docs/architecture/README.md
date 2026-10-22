<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents project documentation.

-->

# Architecture decisions

This directory records cross-component decisions for the openi2pd fork.

## ADR-001 — Supervised logging worker; monolithic core remains interim

- **Status:** In progress; logging and opt-in storage are split, while the
  network/crypto/router-core split remains a tracked implementation task.
- **Context:** I2P session state, tunnel crypto, netDb persistence and the event
  loop are coupled to process-wide singletons. Splitting a state machine
  without clear ownership risks nonce reuse, lost retransmissions and
  inconsistent tunnel state.
- **Decision:** Keep the router runtime monolithic for now, but extract the
  logging writer into an exec'ed worker over a versioned
  `AF_UNIX/SOCK_SEQPACKET` channel. A separate supervisor process owns the
  logging worker and forwards structured records; the core never opens the
  configured log file and never owns the logging child.
- **Consequences:** Real reduction of the log-file capability from the core, a
  separate supervisor boundary for the logger, and opt-in router/torrent
  storage workers. The core still combines network, most persistence and
  long-term keys; this is a documented risk and not global privilege
  separation.
- **Follow-up:** `PRIVSEP-005..014` in `TODO.md`.

## ADR-002 — IPC framing and authentication

- **Status:** Implemented for the log channel.
- **Decision:** Binary, network-byte-order frames with magic, version, type,
  request ID and payload length. Payload is field-encoded, never raw C++
  layouts. `SOCK_SEQPACKET` preserves message boundaries. Peer UID is checked
  with `SO_PEERCRED`/`getpeereid`; channel fds are `CLOEXEC`; worker processes
  are exec'ed and close inherited descriptors.
- **Limits:** 64 KiB frame, 8 KiB text, poll timeouts. Descriptor passing is
  limited to typed operations: the log sink, isolated torrent/router storage
  roots and Android SAF broker handles. There is no generic arbitrary-descriptor
  API; storage paths and capabilities are explicit per operation.
- **Consequences:** The protocol can be reused for future workers; each new
  worker needs its own operation allowlist and OS policy.

## ADR-003 — Android identity and toolchain

- **Status:** Implemented / build validation blocked on Android SDK.
- **Decision:** Application ID/namespace `org.openi2pd.router`, UI name
  `openi2pd`, app-specific storage, no WebView console, no broad storage
  permission. Build with OpenJDK 25, Gradle 9.8.1, AGP 9.4.1, compile/target
  SDK 37, minSdk 29 (Android 10), Java source/target 25 (class-file major 69, D8/R8
  converted to DEX), NDK r30 LTS+. Java 25 is intentional and is not lowered to 17;
  see `docs/android-java25.md` for the D8/R8/ART evidence and the AGP 9.4.1
  bundled-Kotlin target workaround.
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
- **Decision:** Use the existing openi2pd identity. Preserve copyright and
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

  i2pd-tui ── local 0600 UNIX socket ──> TUI server thread
              bounded snapshots + typed transit/log-level writes
```

Target process topology and capability matrices are in
`docs/security/privilege-separation.md` and
`docs/security/process-capabilities.md`.
