<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd worker and IPC checklist.
-->
# Supervisor, worker and IPC checklist

Use with `SKILL.md`. It does not replace the architecture document.

## Current boundary

| Component | Real process? | Capability | Status |
|---|---|---|---|
| Core `i2pd` | yes | network + persistence + keys | monolithic |
| Log worker | yes | one log sink / syslog only | implemented; historical Linux runtime evidence only |
| Supervisor | yes | worker lifecycle + log forwarding | implemented for log; fork/restart tests pending |
| Storage worker | opt-in exec | bounded payload/metadata/DHT operations | implemented statically; Linux/OpenBSD negative tests pending |
| Router-storage worker | opt-in exec | four fixed router files under the state root | implemented statically; `privsep.routerstorage` tests pending |
| Network worker | no | — | pending |
| Crypto worker | no | — | pending |
| TUI server | thread in core | local IPC | no process boundary |
| Android router service | same process/service | no router privsep | not claimed |
| Android SAF broker | separate `:torrent_storage` process | app-private document/tree authority | implemented statically; device/provider tests pending |

## Protocol checklist

- Magic/version/type/request-id/payload length are explicit and bounded.
- Frame length matches the seqpacket record exactly.
- Unknown types/versions and duplicate configuration are fatal.
- Text fields reject embedded NUL and are truncated only on encode.
- Descriptor passing is operation-specific, count-checked and rights-minimized.
- Peer UID/SID/session is checked; manual invocation cannot downgrade transport.
- Request/response ids cannot be reused to confuse a pending operation.
- One in-flight transaction per worker where the design says so.
- Timeout, EOF, malformed frame and send failure poison/close the channel.
- No generic privileged path-write or arbitrary descriptor RPC.

## Lifecycle checklist

1. Prepare configuration and capabilities before spawning.
2. Spawn with no inherited handles except the explicit channel/root contract.
3. Child validates fds, closes standard descriptors, applies sandbox.
4. Child acknowledges only after sandbox and configuration succeed.
5. Parent treats failure as fatal for the requested isolation; no fallback.
6. Shutdown stops new work, drains bounded work, sends shutdown, escalates
   SIGTERM/SIGKILL as designed, reaps exactly once and closes descriptors.
7. Restart is bounded and generation-fenced; stale replies cannot authorize a
   new generation.
8. Parent death is detected by EOF/channel loss where required.

## Platform handoff

- OpenBSD: `pledge`/`unveil`, `imsg`, no directory fd after pledge, inherited
  root at exec for the storage supervisor. See `openi2pd-openbsd`.
- Linux: seccomp, Landlock, no-new-privs, capabilities, namespace and pidfd.
- Windows: named pipes, DACL, restricted token, Job Object, AppContainer
  probing. See `openi2pd-linux-windows`.
- Android: the router service is same-process; the SAF broker runs in the
  non-exported `:torrent_storage` process. This is app-private storage
  isolation, not router-wide privilege separation; device/provider validation
  remains pending.

## Tests to prepare

- `test-privsep-protocol`: codec bounds and malformed frames.
- `test-privsep-linux-ipc` / `test-privsep-linux-sandbox`: peer credentials,
  descriptor transfer, sandbox and queue pressure.
- `test-privsep-openbsd-imsg` / `test-privsep-openbsd-policy`: imsg framing and
  role policy.
- `test-privsep-windows-ipc`: pipe/DACL/peer checks.
- `test-privsep-log`, `test-torrent-storage-*`: worker lifecycle and storage
  capability.

Add labels `ipc;security;platform`; platform-only tests must register only on
their host. Do not execute them locally.
