<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: android jni status.

Documents project documentation.

-->

# Android JNI status and log snapshots

This page is a historical snapshot of the first status/log and administration
methods. The authoritative declaration list is
`android/app/src/main/java/org/openi2pd/router/I2PD_JNI.java` with the generated
header `android/app/src/main/cpp/org_openi2pd_router_I2PD_JNI.h`; the
compile-free `android/tools/check-android-jni-symbols.pl` check keeps all 43
native declarations, header declarations and C++ definitions in parity. It now
also covers process-role selection, SAF storage, torrent metadata/commands,
address-book administration, transport/tunnel controls, data directory, ABI
and network-state callbacks. The subset below is retained for history:

```java
public static native String getStatusSnapshot();
public static native String getRecentLogs(long afterSequence, int limit);
public static native String requestReseed();
public static native String getReseedStatus();
public static native boolean setRuntimeLogLevel(String level);
public static native String getTunnelServices();
public static native String startTunnel(String name); // returns the typed outcome
public static native String stopTunnel(String name); // returns the typed outcome
public static native String addAddressWithResult(String name, String destination, boolean replace);
public static native String getSubscriptions();
public static native String addSubscription(String url);
public static native String removeSubscription(String url);
public static native String banRouter(String base64Hash); // returns the typed outcome
public static native String unbanRouter(String base64Hash); // returns the typed outcome
```

The current methods are implemented in
`android/app/src/main/cpp/i2pd_android.cpp` and declared in the generated
header. See [SAF storage](torrent/saf-storage.md) and
[process capabilities](security/process-capabilities.md#android) for the storage and
process boundaries.

Native start and stop share `NativeCallGuard` with these status/control calls.
`DaemonLifecycle.h` owns the `Stopped -> Starting -> Running` transition;
the inner daemon adapter invokes the real start operation while `Starting`
is already published. A failed start after successful initialization attempts
teardown and restores `Stopped`. The host `test-android-daemon-lifecycle`
regression uses the same transition helper with injected operations. JNI
linking and device startup/restart validation remain pending manual CI.

The status/log methods expose only aggregate counters, the local router's
published/tested transport addresses, and the bounded log ring. No private
keys, router identity material, pointers, session objects or raw memory are
returned. `requestReseed()` accepts no URL or path, returns the non-blocking
`started|already-running|disabled` outcome and never blocks the service worker;
`getReseedStatus()` returns the bounded phase/error/timestamp fields.
`setRuntimeLogLevel()` accepts only the exact case-sensitive set `none`,
`critical`, `error`, `warn`, `info`, `debug` and never writes the configuration
file. The administration calls are a closed typed set: `startTunnel`/
`stopTunnel` take a configured name (≤64 bytes) and never edit a tunnel file;
`addAddressWithResult` reports `Exists` and only replaces when the explicit
`replace` flag is set; subscription and ban calls take a validated URL
(≤512 bytes) or a 44-character base64 router hash and go through the
daemon-owned queue. Every string is length-checked before UTF-8 conversion, and
all calls return a failure when the router is not running. They are typed
native boundary calls, not a generic control channel.

## `getStatusSnapshot()`

Returns newline-separated `key=value` lines. The current implementation emits
these keys in this order:

```text
status=OK
statusCode=0
error=None
errorCode=0
uptime=123
online=1
ntcp2=1
ssu2=1
peers=4
routers=250
inbound=1
outbound=2
transit=3
reseeding=0
inRate=1234
outRate=5678
inRate15=1200
outRate15=5000
totalIn=100000
totalOut=90000
totalTransit=1234
ipv4=NTCP2|1.2.3.4|1234|1;SSU2|1.2.3.4|1234|0
ipv6=NTCP2|[2001:db8::1]|1234|1
```

There is no trailing newline. Values never contain a newline or a NUL byte.

### Fields

| Key | Type | Meaning |
|---|---|---|
| `status` | string | Router status name: `OK`, `Firewalled`, `Unknown`, `Proxy`, `Mesh` or `Stan`. Not localized. |
| `statusCode` | integer | Numeric `i2p::RouterStatus` (0..5). |
| `error` | string | Last router error name: `None`, `ClockSkew`, `Offline`, `SymmetricNAT`, `FullConeNAT`, `NoDescriptors` or `Unknown`. Not localized. |
| `errorCode` | integer | Numeric `i2p::RouterError` (0..5). |
| `uptime` | integer | Seconds since router context construction. |
| `online` | 0/1 | Transport layer online flag. |
| `ntcp2` | 0/1 | NTCP2 listener bound. |
| `ssu2` | 0/1 | SSU2 listener bound. |
| `peers` | integer | Current transport peers. |
| `routers` | integer | RouterInfos currently in NetDb memory. |
| `inbound` | integer | Inbound tunnels. |
| `outbound` | integer | Outbound tunnels. |
| `transit` | integer | Transit tunnels. |
| `reseeding` | 0/1 | NetDb reseed in progress. |
| `inRate` | integer | Current inbound bytes/second. |
| `outRate` | integer | Current outbound bytes/second. |
| `inRate15` | integer | Inbound bytes/second over the last 15 seconds. |
| `outRate15` | integer | Outbound bytes/second over the last 15 seconds. |
| `totalIn` | integer | Total received bytes. |
| `totalOut` | integer | Total sent bytes. |
| `totalTransit` | integer | Total transit bytes transmitted. |
| `ipv4` | address list | IPv4 addresses, see below. Empty when none. |
| `ipv6` | address list | IPv6 addresses, see below. Empty when none. |

Counters are signed 64-bit safe at the Java boundary; the native sources are
`uint32_t`, `uint64_t`, `int` and `size_t`.

### Address list format

An address list is a `;`-separated list of records:

```text
transport|host|port|published
```

- `transport` is `NTCP2` or `SSU2`. Unknown transport styles are omitted.
- `host` is the textual IP address. IPv6 hosts are enclosed in `[` `]`; IPv4
  hosts are not. This mirrors the bracketed form used by the contract example.
- `port` is the decimal port.
- `published` is `1` when the local RouterInfo marks the address as published,
  `0` for a reachable/testing address that is not published.

Addresses whose host is unspecified (`0.0.0.0` or `::`) are omitted. Those are
local bindings, not observed external addresses. The list reflects the local
RouterInfo's current address array; the order matches
`eNTCP2V4Idx`, `eNTCP2V6Idx`, `eSSU2V4Idx`, `eSSU2V6Idx`,
`eNTCP2V6MeshIdx` with transport/family filtering applied. Yggdrasil mesh
addresses therefore appear in `ipv6` as `NTCP2`.

The address fields are sampled best-effort. They are not a point-in-time
snapshot of the RouterInfo and may change while the string is being built.

## `getRecentLogs(long afterSequence, int limit)`

Returns zero or more records, one per line, with no trailing newline:

```text
sequence|timestampSeconds|level|text
```

Example:

```text
41|1730000000|4|Router: started
42|1730000001|3|NetDb: reseed required
```

- `afterSequence` selects records with `sequence > afterSequence`. Negative
  values are treated as `0`. Sequence numbers start at 1 for a process and are
  monotonic.
- `limit` is clamped to **1..256**. A non-positive limit becomes `1`.
- `timestampSeconds` is Unix time in seconds (`std::time_t`).
- `level` is numeric: `0` none, `1` critical, `2` error, `3` warning,
  `4` info, `5` debug.
- `text` is the copy produced by `Log::Append`: at most 512 bytes and with
  control characters below `0x20` (except tab) replaced by spaces. It is
  already sanitized for transmission and is not re-escaped here. A message may
  itself contain `|`, so parsers must split on the first three `|` characters
  only, or use a four-part split with a limit.

The ring holds at most 512 records. Records older than the ring are lost even
if `afterSequence` is older than them; the returned sequence numbers show the
gap explicitly. An empty result is returned as the empty string.

## Threading and lifetimes

- Both entry points may be called from any thread. The service calls them from
  its own executor, off the main thread.
- NetDb counts, transport peer count, tunnel counts and the log ring use their
  existing internal synchronization.
- The status snapshot is not atomic across fields. Individual values are
  internally consistent, but a reader can observe, for example, a peer count
  from before a tunnel count update. Treat the result as a sample, not a
  transaction.
- The SAF bridge holds one process-lifetime global class reference; no Java
  object, activity or listener is retained by
  native code. Each call creates one local `jstring` (returned) and, for
  `getRecentLogs`, a temporary C++ vector that is destroyed before returning.
- The caller owns the returned string. Repeated calls are independent.

## Failure behavior and defensive parsing

The snapshot/log builders catch `std::exception` and unknown exceptions.
String conversion is bounded and returns `null` on failure. C++ allocation
exceptions at the remaining outer JNI entry points are not universally
wrapped; that is a device/CI check, not a claim that no exception can cross.

- `getStatusSnapshot()` returns a fixed, bounded error snapshot with the same
  keys and ordering:
  `status=Error`, `statusCode=-1`, `error=snapshot-failed`,
  `errorCode=-1`, all counters `0`, and empty address lists.
- `getRecentLogs()` returns the empty string on failure.
- Native strings are decoded from ordinary UTF-8 to UTF-16 and passed to
  `NewString`; `NewStringUTF` is not used. Embedded NUL, supplementary
  characters and malformed sequences therefore do not depend on modified UTF-8
  rules. If conversion or VM allocation fails, the pending JNI exception is
  cleared and the method returns `null`.

Java must therefore parse defensively:

- Handle a `null` return as "unavailable" and keep the previous state.
- Tolerate missing, unknown, duplicated or reordered keys; do not rely on the
  key order above.
- Ignore blank lines and malformed lines.
- Validate integer parsing; a value may be absent or out of the expected range
  after an error snapshot.
- Treat empty `ipv4`/`ipv6` as an empty list, not as a parse error.
- For log records, require at least four fields; keep the remainder of the
  line, including any `|`, as the message text.
- Never display a null/empty snapshot as "stopped" by itself; combine it with
  the service start/stop state.

## Security notes

- No private keys, NTCP2/SSU2 static keys, destinations, identities, router
  hashes, file paths from configuration, or heap addresses are included.
- Addresses are the local router's own public/tested transport endpoints.
- Log text is bounded and control characters are flattened by `Log::Append`.
  It may still contain operational details such as peer counts or file names
  already chosen for logging; the UI must render it as plain text and must not
  interpret it as HTML or a format string.
- Status/log methods are read-only. The two runtime admin methods accept only a
  typed boolean/closed-set value, take no path or URL, and require no additional
  permission beyond the app process. They are serialized by the existing
  `NativeCallGuard` and must run on the service worker.
- A process claims exactly one native role (`Router` or `StorageBroker`) before
  its first router- or document-capable entry. `startDaemon` and the SAF bridge
  require `Router`; `restrictSafStorageProcess` claims `StorageBroker` and is
  irreversible. A conflict returns the fixed token `process-role-conflict` and
  never starts a second router.
- The string boundary never passes a null `jchar*` to `NewString` for an empty
  result, clears pending exceptions on `GetStringChars`/array failures, deletes
  local references on every `buildTorrentMetadata` failure and returns its
  documented null fallback.
- `stopDaemon` publishes a native cancellation token before any monitor; a start
  that is inside the asset wait or between `Daemon.init` and `Daemon.start`
  aborts and returns `start-canceled`. The Java start maps that to stopped with
  at most one rollback stop. SAF payloads are detached on the stop path; the
  bridge/payload destructors do not attach a thread or perform JNI after the
  VM lifetime ends.

## Validation status

Static review and `git diff --check` were performed on the host. An earlier
revision of `i2pd_android.cpp` passed a host `clang++ -std=c++23
-fsyntax-only` check and an object-file build; that result predates the current
UTF-8-to-UTF-16, lifecycle and runtime-admin changes and is not current
validation. The JNI symbol declarations were checked against the generated
header, and the Android instrumentation source pins the preference set to the
native closed set. No NDK/Gradle compile or device run has executed for this
increment.

A real Android NDK build, linking, on-device execution and JNI runtime checks
are **not** performed and must be validated in the Android CI/device
environment.

ANDROID-018 adds common daemon initialization rollback through
`daemon/DaemonInit.h`. Phases register before acquisition; reverse cleanup joins
context persistence, revokes storage with a failing writer, releases crypto and
resets the stopped logger destination. Durable keys/configuration and sandbox
effects are retained. Host ledger/log tests are prepared only; production
callback failures, JNI linking and device restart remain pending manual CI.

ANDROID-019 adds a Java owner executor across service recreation. All service
worker submissions share a process FIFO; retirement queues mandatory old-owner
native stop before new-owner commands and rejects stale submissions. Already
accepted operations drain before the stop barrier, retaining their callback
completion paths. Listener snapshots and notification posts are owner-fenced.
The monitor protects only metadata/queue insertion, never JNI; NativeCallGuard
still serializes native calls. JVM regression coverage is prepared, and full
Android service recreation/device validation remains pending manual CI.
