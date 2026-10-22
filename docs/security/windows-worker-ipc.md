<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: windows worker ipc.

Documents project documentation.

-->

# Windows worker IPC and process policy

**Status: static implementation only.** The primitives and tests in this tree
have not been compiled or executed on a Windows host. No service-account,
cross-user, AppContainer/LPAC, Job Object or kernel-ACL enforcement claim is
made by this document.

## Transport choice

The worker transport uses a **local message-mode Named Pipe**:

- `\\.\pipe\openi2pd-worker-<16 hex digits>`; the endpoint is hashed, not
  opened verbatim;
- `PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE`;
- `PIPE_REJECT_REMOTE_CLIENTS`;
- `FILE_FLAG_FIRST_PIPE_INSTANCE`;
- one instance and bounded 64 KiB buffers;
- overlapped `ReadFile`/`WriteFile` with an absolute deadline;
- the common `Protocol.h` frame is carried as one pipe message.

`ncalrpc` with `RPC_C_AUTHN_WINNT` is a valid alternative, but this tree does
not depend on an RPC IDL or an undocumented ALPC interface for worker IPC.
Named Pipes were chosen because the existing versioned frame and operation
allowlists remain unchanged across Linux, OpenBSD and Windows. No TCP or SMB
endpoint is used.

## DACL and SDDL

`WindowsServer::Create` builds an SDDL descriptor equivalent to:

```text
D:P(A;;GA;;;<process-user-SID>)(A;;GA;;;SY)
```

`D:P` marks the DACL protected, so parent inheritance cannot broaden it. The
DACL contains only the process user and local `SYSTEM`; there is no
`Everyone`, `Authenticated Users` or anonymous ACE. The pipe is created with
`bInheritHandle = FALSE`.

The pipe is not a cross-user administration endpoint. If a service runs as
`LocalSystem`, an interactive user cannot connect unless the deployment grants
that principal explicitly. Do not replace this with a `NULL` DACL or
`Everyone`.

## Authentication and authorization

- After `ConnectNamedPipe`, the server obtains the client process ID and session
  ID and opens the client process token.
- The client token user SID must equal the server's process user SID.
- `PIPE_REJECT_REMOTE_CLIENTS` rejects SMB/remote clients; the pipe name itself
  must start with `\\.\pipe\`.
- `WindowsAllowsOperation(role, type)` is the Windows role matrix and the
  dispatcher entry point when worker wiring lands; the POSIX
  `AllowsOperation(EndpointRole, type)` is the corresponding shared matrix. A
  log worker accepts only log/configure/ping/shutdown operations; storage
  request types are present but have no Windows backend yet.
- `WindowsServer::AcceptBound()` requires the accepted client PID/session to
  match the spawned worker when the caller supplies them; `Accept()` remains
  the SID-only/bootstrap form. A rejected connection re-arms the listener
  instead of destroying it, so a same-user impostor cannot trigger a permanent
  worker-channel denial of service.
- `WindowsChannel::ConnectBound()` authenticates the expected server PID and
  session. `MakeRandomPipeName()` uses `BCryptGenRandom` for a per-spawn name;
  the deterministic `MakePrivatePipeName()` remains test-only.
- An oversized or malformed message-mode record closes the pipe. The receive
  buffer is one byte over the maximum legal frame, so every larger record is
  terminal and cannot become a valid prefix plus a separate tail frame.
- Canceled overlapped I/O is drained with `GetOverlappedResult(..., TRUE)`
  before its `OVERLAPPED`/buffer are released; send/receive timeouts close the
  channel rather than leaving a pending operation behind.

The same-account process boundary is not a defense against a compromised user
account. A restricted token, not the pipe DACL alone, must carry the actual
worker restriction.

## Tokens, Jobs, AppContainer and mitigation

`CreateRestrictedWorkerToken` creates a `CreateRestrictedToken` handle and
deletes every privilege enumerated from the parent token (not merely disabling
already-enabled ones). It adds `LUA_TOKEN` when the parent is elevated.
`CreateWorkerJobObject` creates a Job Object with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, `DIE_ON_UNHANDLED_EXCEPTION`, optional
active-process/process/job memory limits and basic UI restrictions.
`SpawnWindowsWorker`:

- creates the restricted token and optional Job Object;
- creates the child suspended with `bInheritHandles = FALSE`;
- passes only the local pipe name on the command line;
- applies `PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY` where available (DEP,
  SEHOP, force-relocate images);
- assigns the process to the Job Object before resuming it.

No handle is inherited and no `DuplicateHandle` transfer is performed. The
worker reconnects by name.

AppContainer and LPAC are still **not implemented**: the current probe is
necessary but not sufficient because `CreateAppContainerToken`,
`TokenIsAppContainer`/`TokenIsLessPrivilegedAppContainer`, capability SIDs and
profile cleanup are not exercised. A policy requesting AppContainer/LPAC fails
closed. Restricted tokens and Job Objects alone do not enforce network or
filesystem separation; they are process-restriction primitives, not a complete
sandbox. The Windows worker entry point and daemon wiring also remain pending,
so no Windows worker can yet start or reconnect.

## Networking and storage separation

`GetWindowsWorkerPolicy` defines the intended role matrix:

- storage: no external-network policy, persistent writes allowed only through
  an explicitly delegated data-directory ACL;
- networking: external sockets allowed, persistent writes disabled;
- supervisor/log: no network and no persistent writes (Linux bootstrap is
  capability-only so exec'ed children can apply their own role policy);
- crypto/admin: no network and no persistent writes.

These are policy profiles, not a claim that the storage/network workers are
already migrated. The network and storage backends remain in the core process.
Only the IPC, DACL, token and Job Object foundations are implemented here.

## Tests

`tests/test-privsep-windows-ipc.cpp` is a Windows-only CTest target. It covers:

- protected DACL inspection: no `Everyone`, `Authenticated Users` or anonymous
  ACE;
- local pipe-name generation and rejection of a remote UNC pipe;
- message-mode Ping round trip;
- peer PID/SID/session inspection and PID-bound acceptance;
- unknown protocol type rejection, oversized-message pipe termination and
  recovery after close;
- restricted-token and Job Object creation;
- isolation capability probing.

It does not prove a second user's access denial, a Windows service account,
AppContainer/LPAC execution, mitigation enforcement, or a live storage/network
worker. Those require a dedicated Windows host and the manual workflow.

## Known limitations

- No current Windows compilation/CTest run exists for this change.
- AppContainer/LPAC is not implemented; requesting it fails closed.
- The worker backends and typed storage/network operations are pending.
- A restricted token plus Job Object does not restrict network access or file
  paths by itself; the deployment must use AppContainer/LPAC or an equivalent
  OS-enforced boundary before claiming full separation.
- DACL/owner policy is same-account oriented; it is not an administrative
  boundary and does not protect against a compromised account or the service
  process itself.
- `ncalrpc` remains an evaluated alternative, not a second protocol
  implementation. No undocumented ALPC API is used.
- `SpawnWindowsWorker` still inherits the parent environment and current
  directory; a minimal environment block and explicit install-directory CWD
  remain pending.
- The Windows `--privsep-worker` entry point, supervisor/restart owner and
  daemon wiring do not exist yet, so no Windows worker can currently start.

## Manual CI matrix

The Windows workflows remain `workflow_dispatch`-only. Prepare these lanes
without changing triggers:

| Lane | Check |
|---|---|
| MSYS2 CLANG64 | Configure/build, `test-privsep-windows-ipc`, random pipe names, server-bound connect, framed round trip, oversized/malformed termination |
| MSVC 2022 | Same tests with `/W4`; inspect Job/UI restrictions and restricted-token creation |
| Elevated parent | Restricted-token privileges are deleted; `LUA_TOKEN` path is exercised; worker cannot re-enable a deleted privilege |
| Standard user | `CreateProcessAsUser` privilege failure is reported explicitly, with no fallback worker |
| Second local user | Pipe DACL rejects connection; listener remains usable and accepts the real worker |
| Service account | Start/stop/restart ordering, Job `KILL_ON_JOB_CLOSE`, bounded stop and exit status |
| AppContainer/LPAC request | Fails closed with a reason; no silent downgrade to a restricted token |
| Overlapped stall | Stalled reader/writer returns a bounded timeout, closes the channel, and does not corrupt freed buffers |

## Transport decision: Named Pipes vs ncalrpc

Message-mode Named Pipes remain the worker transport. They preserve the shared
`Protocol.h` frame/codec, need no MIDL-generated stubs, build on both the MSYS2
and MSVC lanes, and already fail closed on remote/UNC names. `ncalrpc` provides
per-call SSPI identity and an explicit local-only interface flag, but rpcrt4 has
no documented handle transfer, requires MIDL/IDL or checked-in generated stubs,
and introduces a separate concurrency/cancellation model. Revisit ncalrpc only
as a second transport if a genuine cross-principal service/user or AppContainer
boundary is required; do not replace pipes merely to rename the primitive.
