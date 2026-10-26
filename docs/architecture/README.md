<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents project documentation.

-->

# Architecture decisions

This directory records cross-component decisions for the openi2pd fork.

## ADR-001 — Supervised logging worker; monolithic core remains interim

- **Status:** In progress; logging, fixed-file storage and (opt-in) hash-named
  runtime artifacts are split, while the network/crypto/router-core split
  remains a tracked implementation task.
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
- **Follow-up:** `PRIVSEP-005..021` in `TODO.md`; ADR-005 records the
  capability invariant and the artifact boundary.

## ADR-005 — Capability invariant, spawn identity and artifact storage

- **Status:** Implemented / static (Pending CI compilation and tests). The
  invariant is declared and enforced by policy for worker roles; the router
  core still violates it until the network/crypto split lands.
- **Decision:** No process role may combine `ExternalNetwork` with
  `PersistentWrite`. `privsep/Capabilities.h` is the single role×capability
  matrix; every OS backend policy must be reviewable against one row, and a
  target row is rejected at compile time if it sets both bits. The legacy core
  row is recorded explicitly (with the write bit) so the gap cannot be mistaken
  for compliance.
- **Spawn identity:** a worker role is bound to its spawner by a 16-byte
  CSPRNG nonce delivered on a dedicated pipe (child fd 5, closed before any
  frame or sandbox) rather than on `argv`, so a same-UID process cannot invoke
  `--privsep-worker <role> 3` with its own socketpair and adopt a role.
- **Artifact boundary:** when `privsep.artifacts` is enabled, the core stops
  writing the netDb RouterInfo cache, peer profiles and destination private
  keys. They are written by a data-storage worker that owns only the
  hash-named artifacts under a validated root, with no network capability. The
  writer is the only path: an installed writer failure never falls back to the
  filesystem, and an enabled-but-missing worker fails closed. Startup reads
  remain direct until the read path is split.
- **Generation fencing:** every storage channel is configured with a monotonic
  generation; requests are fenced per generation, replies from a previous
  generation are dropped, an ambiguous write is never replayed, and a failed
  client restarts with bounded backoff (250 ms→30 s, 8 attempts in 60 s,
  stable reset after 60 s) where the platform allows an exec'ed worker.
- **Consequences:** a parser compromise in the network side no longer reaches
  the runtime write authority for these artifacts when the option is enabled.
  The core still owns transport sockets and (at bootstrap) router keys, so the
  invariant is not yet global; the remaining phases are the network worker
  (listener/connect handoff), the crypto/signing worker, the read path and
  Windows/OpenBSD parity.
- **Follow-up:** `PRIVSEP-018..022` in `TODO.md`.

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
