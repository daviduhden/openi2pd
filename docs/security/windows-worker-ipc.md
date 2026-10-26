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
- `WindowsAllowsOperation(role, type)` is derived from the shared registry and
  is enforced by `WindowsChannel::Receive` after the role is bound: a role may
  only receive the request-direction types the registry grants it, a
  `Required*` descriptor type is always refused (Windows has no descriptor
  channel), and a refusal is one typed `Error` under the eight-violation
  budget. `WorkerHello` is the only request the server reads before a role is
  bound; it carries the role and the 32-byte spawn nonce.
- `WindowsServer::AcceptBound()` requires the accepted client PID, session,
  user SID and (when the role uses AppContainer) the exact container SID; it
  impersonates the client to read `TokenUser`/`TokenIsAppContainer`/
  `TokenAppContainerSid`/`TokenIntegrityLevel`, then reverts. It then consumes
  one `WorkerHello` with the expected role and nonce before returning the
  channel. Rejections re-arm with a bounded backoff (16 per accept) so a
  same-user impostor cannot starve the real worker. `Accept()` is test-only.
- `WindowsChannel::ConnectBound()` authenticates the expected server PID and
  session. `MakeRandomPipeName()` uses `BCryptGenRandom` for a per-spawn name;
  the deterministic `MakePrivatePipeName()` remains test-only.
- The pipe DACL is protected and contains only the service user, SYSTEM and,
  when the role uses AppContainer, its exact container SID. There is no
  Everyone, Authenticated Users or ALL APPLICATION PACKAGES entry.
- An oversized or malformed message-mode record closes the pipe. The receive
  buffer is one byte over the maximum legal frame, so every larger record is
  terminal and cannot become a valid prefix plus a separate tail frame.
- Canceled overlapped I/O is reaped with `GetOverlappedResult` after
  `CancelIoEx`: a record that completed before the cancellation is delivered,
  not discarded. A timed-out write closes the channel instead of being retried
  on a new one.

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

`WindowsIsolation.cpp` is the role authority. Storage, log, crypto and admin
use a low-privilege AppContainer (no `internetClient`); network uses a
non-low-privilege AppContainer with `internetClient` and
`privateNetworkClientServer`; the supervisor uses the restricted token because
it must spawn children. `CreateWindowsAppContainer` creates or derives the
durable per-role profile (`openi2pd.<role>`) and capability SIDs; `GrantApp
ContainerPath` grants only the delegated write root to the container SID. The
role table rejects any role that combines an Internet capability with a write
root. `SpawnWindowsWorker` refuses a container role without a container, a
container for a non-container role, or a persistent-write role without a
delegated root, and uses `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` plus the
LPAC all-applications opt-out for containers. A restricted token is never
silently substituted for a role that requires a container. Profiles are durable
and are not deleted automatically; cleanup is an operator action.

## Networking and storage separation

The AppContainer capability set is the boundary:

- storage/log/crypto/admin: no `internetClient`; storage/log receive one
  delegated write root; crypto/admin have no write root;
- network: `internetClient` and `privateNetworkClientServer`, no delegated
  write root; it can still write inside its own AppContainer profile folder,
  which is a documented residual;
- supervisor: restricted token, no container, no network/write capability.

These are still role profiles for the future worker backends: the storage and
network engines remain in the core process today. The IPC, DACL, token,
AppContainer, Job Object and supervisor foundations are implemented; no live
storage/network worker is wired into the daemon yet.

## Tests

`tests/test-privsep-windows-ipc.cpp`, `test-privsep-windows-isolation.cpp` and
`test-privsep-windows-supervisor.cpp` are Windows-only CTest targets. They
cover:

- protected DACL inspection: no `Everyone`, `Authenticated Users`, anonymous
  or ALL APPLICATION PACKAGES ACE;
- local pipe-name generation, remote/UNC rejection and pre-created-name
  refusal;
- message-mode Ping round trip, role-bound per-operation refusal, exact
  maximum frame versus one byte over, and recovery after close;
- peer PID/SID/session inspection, wrong PID/session/nonce/role rejection,
  AppContainer SID matching and listener re-arm;
- completed-during-cancel delivery and stale-write closure;
- restricted-token, AppContainer profile/SID, capability-set and delegated-root
  grant inspection;
- Job Object limits/mitigations, supervisor restart/backoff, worker kill and
  handle-leak checks, and the service stop budget.

It does not prove a second user's access denial, a Windows service account,
AppContainer/LPAC execution, mitigation enforcement, or a live storage/network
worker. Those require a dedicated Windows host and the manual workflow.

## Known limitations

- No current Windows compilation/CTest run exists for this change.
- The worker backends and typed storage/network operations are pending, and no
  daemon path instantiates the supervisor yet.
- AppContainer profiles are durable and operator-owned; the code never deletes
  one automatically.
- The nonce travels on the worker command line, so it is not a secret from a
  same-user process that can read the argument block; the protected DACL plus
  PID/session/container binding remain the primary boundary, and the nonce
  binds the connection to one spawn.
- A network AppContainer can write inside its own profile folder; only the
  delegated data root is protected.
- DACL/owner policy is same-account oriented; it is not an administrative
  boundary and does not protect against a compromised account or the service
  process itself.
- `ncalrpc` remains an evaluated alternative, not a second protocol
  implementation. No undocumented ALPC API is used.
- `SpawnWindowsWorker` still inherits the parent environment and current
  directory; `CREATE_NO_WINDOW` and NUL std handles are set, but a minimal
  environment block and explicit install-directory CWD remain pending.
- The Windows `--privsep-worker` entry point and `WindowsWorkerSupervisor` exist,
  but no daemon path starts a Windows worker yet, so production wiring remains
  pending.

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
| AppContainer/LPAC | Profile/SID/capability/root-grant inspection; storage has no socket, network has no write root; no silent downgrade to a restricted token |
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
