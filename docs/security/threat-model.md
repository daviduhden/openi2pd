# Privilege-separation threat model

Scope: current tree and target multiprocess architecture. This is a static
analysis; no OpenBSD/Windows/Android capability tests have been executed.

## Assets

- Router long-term private keys and destination keys.
- Session keys, ratchet state and nonces.
- Router identity and anonymity sets.
- netDb, RouterInfo, LeaseSets and peer profiles.
- Logs (may contain peer addresses and operational metadata).
- TUI administrative channel.

## Adversaries

1. Remote peer sending malformed NTCP2/SSU2/I2NP data.
2. Malicious persisted RouterInfo/LeaseSet/SU3/profile.
3. Local same-UID process (already inside the key trust boundary).
4. Compromised network worker.
5. Compromised storage worker.
6. Compromised router core.
7. Malicious TUI/IPC client.
8. Resource-exhaustion attacker.

## Scenario matrix

| Scenario | Attacker capabilities | Lateral movement | Secrets exposed | Current containment | Residual risk |
|---|---|---|---|---|---|
| Network worker compromised by packet | Network sockets, parser memory, IPC to core | Can send malicious normalized events to core; cannot write files if target policy enforced | Session keys in worker if not moved; no long-term keys if crypto split | **Not implemented** | Core and network are same process today; full impact |
| Storage worker compromised | Files under data dir, IPC to core | Can return malicious RouterInfo/LeaseSets/profiles; cannot open Internet if pledge enforced | Persisted keys if storage can read them | **Not implemented** | Arbitrary file read/write via current core |
| Router core compromised via I2NP | Routing state, IPC clients | Request storage/network operations through typed APIs; cannot directly access both if policy enforced | Keys if held in core; tunnel/session material | **Not implemented** | Same process has all capabilities |
| Admin IPC compromised | TUI socket (0600, same UID), read-only requests | Status/log reads; no write commands in v1 | Public status/log data only | Implemented read-only protocol, bounds/UID check | Logs/metadata disclosure to same UID |
| Log worker compromised | Log file/syslog, IPC channel | Cannot reach network/keys under policy; can corrupt logs | Log contents only | Implemented separation | Bounded in-process restart monitor; same-UID worker remains trusted |
| Malicious IPC messages | Frame injection on inherited channel | Type confusion, oversized allocation, command injection | Depends on worker | Bounded parser, exact lengths, type allowlist, peer UID | No fuzzing yet |
| Queue saturation | Flood of log records/IPC | Memory/CPU pressure, dropped logs | None directly | Bounded frame/text; 1 s sink send timeout closes the channel on backpressure | Logger queue is still the existing in-core queue; no per-worker or disk quota yet |
| Worker crash/restart | Kill worker or crash | Supervisor restart storms, missing service | Logs if log worker dies | Log worker failure drops logs, no core file fallback | Bounded restart (500 ms..30 s, max 8 attempts) then logs are dropped; no separate supervisor process |

## Key-flow constraints

- Long-term keys must not be sent to the network worker.
- A crypto worker must not expose arbitrary signing/decryption: requests carry
  key role, operation, context and expected output format.
- Storage must not expose generic paths; operations name resources by type and
  validated identifier.
- Logs and admin responses must never include private keys, nonces, tags or
  raw key material.

## Failure and denial of service

- IPC frames are capped at 64 KiB and text at 8 KiB.
- Logging is asynchronous in the core; a blocked log worker can consume the
  logger queue but must not block packet processing. Current implementation
  uses the existing logging queue; a dedicated bounded channel queue is
  pending.
- A worker that fails protocol validation exits; the core keeps running and
  drops log records until the bounded in-process monitor restarts the worker.
- Restarts use 500 ms exponential backoff capped at 30 s, stop after eight
  consecutive attempts, and reset after 60 s of stable operation. There is no
  unbounded restart loop and no separate supervisor process; exhausting the
  attempts leaves logging stopped with no file fallback in the core.

## Out of scope / limitations

- Same-UID local processes remain inside the trust boundary; process separation
  protects against remote compromise escalation, not against a local attacker
  who can read the data directory.
- Android ART/JNI process model is not equivalent to desktop process
  separation.
- Windows worker isolation is not implemented.
- No dynamic test has demonstrated that pledge/unveil policies are complete.
