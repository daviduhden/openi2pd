<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: android jni status.

Documents project documentation.

-->

# Android JNI status and log snapshots

This document describes the Android JNI status/log surface and the bounded
runtime administration entries used by the service:

```java
public static native String getStatusSnapshot();
public static native String getRecentLogs(long afterSequence, int limit);
public static native boolean requestReseed();
public static native boolean setRuntimeLogLevel(String level);
```

They are implemented in `android/app/src/main/cpp/i2pd_android.cpp` and
declared in `android/app/src/main/cpp/org_openi2pd_router_I2PD_JNI.h`.

The status/log methods expose only aggregate counters, the local router's
published/tested transport addresses, and the bounded log ring. No private
keys, router identity material, pointers, session objects or raw memory are
returned. `requestReseed()` accepts no URL or path and calls the existing
single-flight NetDb bootstrap; `setRuntimeLogLevel()` accepts only the exact
case-sensitive set `none`, `critical`, `error`, `warn`, `info`, `debug` and
never writes the configuration file. Both return `false` when the router is not
running, and both execute on the service worker rather than the Android main
thread. They are typed native boundary calls, not a generic control channel.

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
- No global JNI references are created and no Java object is retained by
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
