<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: threat model.

Documents project documentation.

-->

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
| Storage worker compromised | Files under delegated data root, IPC to core | Can return malicious RouterInfo/LeaseSets/profiles; cannot open Internet if pledge/seccomp enforced | Only delegated payload/metadata files | Opt-in router-storage and torrent payload/state workers exist with no network capability; core still writes netDb/profiles/address book/keys | Core still has broader filesystem authority; full netDb/profile/key migration pending |
| Router core compromised via I2NP | Routing state, IPC clients | Request storage/network operations through typed APIs; cannot directly access both if policy enforced | Keys if held in core; tunnel/session material | **Not implemented** | Same process has all capabilities |
| Admin IPC compromised | TUI socket (0600, same UID), bounded requests | Status/log/directory reads; typed transit/log/reseed/shutdown/reload/bandwidth/share commands | Public status/log data, operational metadata | Bounded protocol, fixed daemon-owned command queue, UID/pipe DACL check, closed typed admin set; no generic config/path/URL/file/command operation | Logs/metadata disclosure and operational changes by same UID |
| Supervisor compromised | Worker lifecycle/control channel; no log file handle | Can stop/restart log worker | Log metadata/records in transit | Implemented process separation; OpenBSD policy has no network/path access. Linux uses capability-only bootstrap (no-new-privs + capability drop) so exec'ed children can apply their delegated role policy; it does not install an inherited Landlock/seccomp domain | Same-UID supervisor; fork/exec retained for log restarts; Linux supervisor has no enforced network/write domain |
| Malicious IPC messages | Frame injection on inherited channel | Type confusion, oversized allocation, command injection | Depends on worker | Protocol v2 bounded parser, typed `Error`, exact lengths, per-endpoint `AllowsOperation`, strictly increasing storage request IDs, fallible path encoders, peer UID and revalidated adopted channels; OpenBSD imsg adapter preserves the frame checks | Fuzzing and live cross-UID validation pending |
| Queue saturation | Flood of log records/IPC | Memory/CPU pressure, dropped logs | None directly | Bounded frame/text; 1 s sink send timeout closes the channel on backpressure; logger queue capped at 8192 records with 8192-byte truncation and a dropped counter | No disk quota; cumulative drop counter is not surfaced to every administrative client |
| Worker crash/restart | Kill worker or supervisor | Supervisor restart storms, missing service | Logs if log worker dies | Supervisor owns/restarts log worker with exponential backoff, eight-attempt cap and stable reset; without the sandbox the core can replace a dead supervisor; under the OpenBSD runtime policy it cannot exec a replacement supervisor | Exhaustion stops logging; no core compromise protection; router/network/crypto workers not under one supervisor |

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
- Logging is asynchronous in the core; records pass through the supervisor to
  the log worker. A blocked worker can consume the logger queue but must not
  block packet processing. The dedicated bounded per-worker queue is still
  pending.
- A worker that fails protocol validation exits; the supervisor restarts the
  log worker. When the OpenBSD runtime sandbox is active, the core has no
  `exec` capability and does not replace a dead supervisor; it retains `proc` only for bounded reaping/signalling.
- Restarts use bounded backoff. The separate supervisor owns the log worker;
  outside the OpenBSD runtime sandbox the core can replace a dead supervisor.
  Exhausting the attempts leaves logging stopped with no file fallback.
  Network, storage and crypto workers are not supervised yet.

## Out of scope / limitations

- Same-UID local processes remain inside the trust boundary; process separation
  protects against remote compromise escalation, not against a local attacker
  who can read the data directory.
- Android ART/JNI process model is not equivalent to desktop process
  separation.
- Windows worker isolation is not implemented.
- No dynamic test has demonstrated that pledge/unveil policies are complete.
