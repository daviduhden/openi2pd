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
- The protocol dispatcher applies `WindowsAllowsOperation(role, type)` before
  handling a message. A log worker accepts only log/configure/ping/shutdown
  operations; a storage/network operation type is not exposed until its typed
  protocol is implemented.
- The channel's `Peer()` exposes the PID, session, SID and elevation state for
  diagnostics/tests. PID alone is never used as authorization.

The same-account process boundary is not a defense against a compromised user
account. A restricted token, not the pipe DACL alone, must carry the actual
worker restriction.

## Tokens, Jobs, AppContainer and mitigation

`CreateRestrictedWorkerToken` creates a `CreateRestrictedToken` handle with
`DISABLE_MAX_PRIVILEGE`. `CreateWorkerJobObject` creates a Job Object with
`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, optional active-process and process-memory
limits. `SpawnWindowsWorker`:

- creates the restricted token and optional Job Object;
- creates the child suspended with `bInheritHandles = FALSE`;
- passes only the local pipe name on the command line;
- applies `PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY` where available (DEP,
  SEHOP, force-relocate images, ATL thunk disable);
- assigns the process to the Job Object before resuming it.

No handle is inherited and no `DuplicateHandle` transfer is performed. The
worker reconnects by name.

AppContainer and LPAC are probed via documented `userenv.dll` exports, but they
are **not silently substituted**. A policy requesting AppContainer/LPAC fails
closed until the token/profile/ACL and capability setup is implemented and
validated. Restricted tokens and Job Objects alone do not enforce network or
filesystem separation; they are process-restriction primitives, not a complete
sandbox.

## Networking and storage separation

`GetWindowsWorkerPolicy` defines the intended role matrix:

- storage: no external-network policy, persistent writes allowed only through
  an explicitly delegated data-directory ACL;
- networking: external sockets allowed, persistent writes disabled;
- supervisor/log: no network and no persistent writes;
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
- peer PID/SID/session inspection;
- unknown protocol type rejection and recovery after close;
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
