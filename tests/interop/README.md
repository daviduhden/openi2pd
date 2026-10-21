<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents regression and interoperability checks.

-->

# Java interoperability test harness

This directory is a reproducible, **static-first** differential test harness for
the I2P wire protocols. It pins the official I2P Java router as a reference,
checks in only fixtures whose bytes can be derived unambiguously from the
specification and the pinned source, and defines how a C++ harness is compared
against them.

Java is **not** a build or runtime dependency of openi2pd. Nothing here is
compiled into the router or downloaded at runtime. The checked-in fixture
validator is registered as the static CTest test `interop-static`; the pinned
Java checkout and the differential runner remain opt-in because they require
an external, read-only reference tree. No differential test has been executed.

All validation and build/run commands below are manual CI runbooks. The
repository's static-only local policy prohibits executing these validators,
building Java/C++ or running peers locally.

## Current target and historical fixture provenance

| Item | Value |
|---|---|
| Repository | `https://github.com/i2p/i2p.i2p` (official mirror) |
| Current target tag | `i2p-2.14.0` |
| Target commit | `3c98503c5f14efe7bdd783528ba7f621f12d8081` |
| Annotated tag object | `9db233ed7241599eba76f29b8c334eaaf3db2e87` |
| Target checked | 2026-10-21; official remote tag and peeled commit verified; source/parity review and CI remain pending |
| Existing corpus tag | `i2p-2.13.0` |
| Corpus commit | `9134f808337b401e8e53c73734c81fab04280c9d` |

Keep the clone outside this repository and make it read-only:

```sh
git clone --depth 1 --branch i2p-2.14.0 \
  https://github.com/i2p/i2p.i2p.git /var/tmp/i2p-java-2.14.0
chmod -R a-w /var/tmp/i2p-java-2.14.0
I2P_JAVA_REFERENCE=/var/tmp/i2p-java-2.14.0 \
  tests/interop/check-reference.sh
```

`check-reference.sh` verifies the tag, commit, cleanliness and read-only state.
It never modifies the reference tree.

The checked-in JSON/hex fixtures and their Java line citations retain their
original 2.13.0 provenance. `check-vectors.pl --reference` checks that older
source, with `I2P_JAVA_FIXTURE_REFERENCE` as its default environment variable.
It deliberately does not inherit `I2P_JAVA_REFERENCE`, which names the current
2.14.0 target. Updating the target neither re-derives fixtures nor establishes
2.14.0 compatibility. Keep the historical checkout separate:

```sh
git clone --depth 1 --branch i2p-2.13.0 \
  https://github.com/i2p/i2p.i2p.git /var/tmp/i2p-java-2.13.0
chmod -R a-w /var/tmp/i2p-java-2.13.0
```

CMake uses `I2PD_INTEROP_REFERENCE` for the 2.14.0 target and
`I2PD_INTEROP_FIXTURE_REFERENCE` for the historical 2.13.0 corpus. Either
optional checkout may be supplied independently.

## Layout

| Path | Purpose |
|---|---|
| `FIXTURES.md` | Complete fixture matrix for every protocol area, with provenance and capture status. |
| `check-reference.sh` | POSIX `sh` tag/commit/clean/read-only check. |
| `check-vectors.pl` | Static fixture validator and optional differential runner. |
| `vectors/valid/` | Checked-in well-formed inputs and expected parses. |
| `vectors/malformed/` | Checked-in invalid inputs that Java rejects. |
| `vectors/README.md` | Case schema and C++ harness contract. |
| `templates/` | Deterministic rules that contain no wire keys/signatures; documentation for future test targets, not parsed by `check-vectors.pl`. |

## Quick start (static only)

```sh
# Validate the reference tag, commit, cleanliness and read-only state.
I2P_JAVA_REFERENCE=/var/tmp/i2p-java-2.14.0 \
  tests/interop/check-reference.sh

# Validate every checked-in fixture: SHA-256, I2NP size/checksum and I2CP
# length framing. Java provenance citations are skipped when the reference is
# not supplied.
perl tests/interop/check-vectors.pl

# With the historical corpus reference, also check tag/commit/cleanliness and Java
# file/line/substring provenance.
I2P_JAVA_FIXTURE_REFERENCE=/var/tmp/i2p-java-2.13.0 \
  perl tests/interop/check-vectors.pl --require-reference

# List cases without reading inputs.
tests/interop/check-vectors.pl --list

# Check one case.
tests/interop/check-vectors.pl --case i2np-dblookup-ratchet-pubkey
```

The static validator prints `STATIC-OK` per case and ends with:

```text
differential cases not executed: no --binary supplied
```

This is the only validation performed in this repository. It does not prove
that the C++ parser agrees with Java.

## Differential mode

The user supplies a C++ test binary; the script never builds or downloads it:

```sh
tests/interop/check-vectors.pl \
  --reference /var/tmp/i2p-java-2.13.0 \
  --binary /path/to/test-interop-vectors
```

Contract: the binary is invoked once per case as

```sh
test-interop-vectors --case CASE_ID /absolute/path/to/input
```

It must exit 0 and print one JSON object to stdout. It must not touch the
network, the system clock, or the Java tree. The runner compares JSON
semantically and reports `PASS`, `FAIL`, `KNOWN-GAP` or `UNEXPECTED-PASS`.

No differential binary is registered in `tests/CMakeLists.txt`. The intended
future target is `test-interop-vectors` from `tests/test-interop-vectors.cpp`.
It is left unregistered because I2CP/SAM parsing is currently embedded in
client classes and needs a small pure-parser adapter before a self-contained
test can be added safely. I2NP-only cases can already be implemented with
`i2p::GetI2NPMessageLength()` and the `I2NPMessage` accessors.

The static fixtures run in CTest as `interop-static` and in the manual Linux
CI job. The `interop` CI job clones the current 2.14.0 target and historical
2.13.0 corpus source into separate temporary read-only directories, runs
`check-reference.sh` for the target and checks corpus provenance against 2.13.0.
Neither path publishes, downloads into the repository, or contacts a live I2P
peer.

## Fixture policy

- Only deterministic inputs are checked in: field layouts, length framing,
  SHA-256 checksum prefixes, protocol constants and ASCII handshakes.
- Inputs containing signatures, session keys, tags or identities are captured
  by the user from the reference build and stored outside this repository, or
  committed only after the private material is removed.
- Valid and malformed inputs are separate directories. Corpora are bounded.
- Every case has `provenance.spec`, `provenance.java` (`path`, `line`,
  `contains`) and `sha256`. The validator re-checks the Java citations against
  the pinned checkout.
- A Java capture is evidence, not a specification. If Java and the spec
  disagree, record both; do not silently accept Java behavior.
- Do not invent expected Java output. The expected JSON in a case describes
  the fields implied by the cited layout and is labelled as a normalized parse.

See [`FIXTURES.md`](FIXTURES.md) for the full matrix, including everything that
still has to be generated by the user's environment.

The 2026-10-20 functional-parity increment added focused in-process tests for
SSU2 PathChallenge timing, runtime ML-KEM provider gating, RSA identity
verification, fail-closed SU3 defaults, SAM framing/control grammar, I2CP
identity-hash routing keys, address-book case handling and torrent resume/
tracker behavior. Those are CTest regressions, not Java differential evidence:
the Java harness still needs captured signed/fixture material and an isolated
run before any row is promoted from static to validated.

## Runbook

Commands below are runbooks for manually dispatched CI only. No command was
executed for the 2.14.0 target update. Dynamic sections require a disposable,
network-isolated environment.

### 1. Reference and fixture checks — manual CI

```sh
I2P_JAVA_REFERENCE=/var/tmp/i2p-java-2.14.0 \
  tests/interop/check-reference.sh
I2P_JAVA_FIXTURE_REFERENCE=/var/tmp/i2p-java-2.13.0 \
  tests/interop/check-vectors.pl
```

### 2. C++ unit / malformed tests — NOT EXECUTED

```sh
cmake -S build -B build-output -G Ninja \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-output --target \
  test-protocol-bounds test-routerinfo-firewall test-blinding test-eddsa \
  test-aeadchacha20poly1305 test-elligator
ctest --test-dir build-output --output-on-failure \
  -R 'test-(protocol-bounds|routerinfo-firewall|blinding|eddsa|aeadchacha20poly1305|elligator)'
```

Negative/malformed categories are listed in `FIXTURES.md` §1–§9. The checked-in
`vectors/malformed/` cases are exercised only through the future
`test-interop-vectors` binary.

### 3. Fuzzing — NOT EXECUTED

```sh
cmake -S build -B build-fuzz -G Ninja \
  -DBUILD_TESTING=ON -DWITH_FUZZERS=ON \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-fuzz --target fuzz-protocol-bounds fuzz-http-parser fuzz-tui-protocol

# Convert checked-in hex vectors to a raw libFuzzer corpus.
mkdir -p /tmp/opencode/interop-corpus/protocol-bounds
for f in tests/interop/vectors/malformed/*.hex; do
  perl -0777 -ne 's/^[ \t]*#.*$//mg; s/\s+//g; print pack "H*", $_' "$f" \
    > "/tmp/opencode/interop-corpus/protocol-bounds/$(basename "$f" .hex)"
done
build-fuzz/fuzz-protocol-bounds \
  -max_total_time=60 -artifact_prefix=/tmp/opencode/interop-artifacts/ \
  /tmp/opencode/interop-corpus/protocol-bounds
```

Do not add the fuzz targets to the default build or to CTest.

### 4. Isolated SAM / I2CP loopback — NOT EXECUTED

Start one openi2pd router with its public network features disabled. Binding to
loopback and setting a private netid prevents accidental public traffic:

```sh
mkdir -p /var/tmp/i2pd-interop
build-output/i2pd \
  --datadir=/var/tmp/i2pd-interop \
  --netid=42 \
  --ipv6=0 \
  --reseed.urls= \
  --httpproxy.enabled=0 \
  --socksproxy.enabled=0 \
  --bob.enabled=0 \
  --tui.enabled=0 \
  --sam.enabled=1 --sam.address=127.0.0.1 --sam.port=17656 \
  --i2cp.enabled=1 --i2cp.address=127.0.0.1 --i2cp.port=17654
```

Replay the checked-in SAM hello vectors against the local bridge:

```sh
for f in tests/interop/vectors/valid/sam-hello-*.txt; do
  perl -MIO::Socket::INET -e '
    my ($host,$port,$file)=@ARGV;
    open my $in,"<",$file or die $!; binmode $in;
    my $req = do { local $/; <$in> };
    my $s = IO::Socket::INET->new(PeerAddr=>$host,PeerPort=>$port,Proto=>"tcp") or die $!;
    print $s $req; my $reply = <$s>; print "$file -> $reply";
  ' 127.0.0.1 17656 "$f"
done
```

For I2CP, connect a client to `127.0.0.1:17654` and send the checked-in
`i2cp-hostlookup-hash` / `i2cp-blindinginfo-hash` messages. A raw I2CP session
must first send `CreateSessionMessage`; the checked-in messages are body-level
vectors for the parser target, not a complete session script.

To compare against Java, start the Java router separately on a different
loopback port and point the client at it. The harness does not build Java; use
the reference's own procedure (`ant build` or the Gradle wrapper) in a
throw-away directory.

### 5. Isolated router-to-router NTCP2 / SSU2 — NOT EXECUTED

Requirements:

- two disposable hosts or two loopback addresses (for example `127.0.0.2` and
  `127.0.0.3`), never the public interface;
- the same private netid on both sides (`--netid=42` for openi2pd,
  `router.networkID=42` in Java's `router.config`);
- reseed disabled and no address-book subscriptions;
- a pre-seeded `netDb/routers/` containing only the two RouterInfos, or a local
  test driver that injects them. Do not use public reseed or public peers.

Start openi2pd with an explicit private address and no reseed:

```sh
build-output/i2pd \
  --datadir=/var/tmp/i2pd-interop \
  --netid=42 \
  --host=127.0.0.3 --port=17601 \
  --address4=127.0.0.3 \
  --ipv6=0 \
  --reseed.urls= \
  --sam.enabled=0 --i2cp.enabled=0 \
  --httpproxy.enabled=0 --socksproxy.enabled=0 --bob.enabled=0 \
  --tui.enabled=0
```

Start the Java reference with the same netid and its own loopback address,
using the reference's built classpath and a disposable config:

```sh
mkdir -p /var/tmp/i2p-java-interop
printf 'router.networkID=42\n' >> /var/tmp/i2p-java-interop/router.config
java -cp '/var/tmp/i2p-java-2.14.0/lib/*' \
  -Di2p.dir.base=/var/tmp/i2p-java-2.14.0 \
  -Drouter.configLocation=/var/tmp/i2p-java-interop/router.config \
  net.i2p.router.RouterLaunch
```

Seed exchange (paths depend on the reference packaging; verify them before
running):

```sh
# Copy only the two test RouterInfos between the two netDb directories, then
# restart both routers. Never copy a public netDb snapshot.
ls /var/tmp/i2pd-interop/netDb/routers/
ls /var/tmp/i2p-java-interop/netDb/routers/
```

Then exercise, on the loopback link only:

- NTCP2 `v=2` and PQ `v=4` establishment, reconnect, and termination;
- SSU2 TokenRequest/Retry/SessionRequest/SessionCreated/SessionConfirmed,
  relay `ver=2`, peer test, path challenge/response, MTU/IPv6;
- SSU2 fragments 1..15 and 32 KiB message reassembly;
- ACK ranges, lost ACK0, duplicate packet 0;
- wrong-netid packets (must be rejected with reason 21).

Capture normalized observations: address family, negotiated version, PQ flag,
packet/connection IDs, block types and lengths, fragment counts, ACK ranges,
reason codes. Store no live keys.

### 6. Ratchet, Streaming, Datagram, SAM and I2CP end-to-end — NOT EXECUTED

Use the same isolated pair or a local client against the Java router:

- ratchet NS (minimum 103 bytes), DateTime skew −5/+2 minutes, duplicate
  ephemeral replay, NSR retransmission and PQ NSR;
- Streaming SYN/SYN-ACK/RESET/close;
- Datagram2 minimum 429 bytes, offline signatures, options; Datagram3 first
  contact without a cached LeaseSet;
- SAM 3.0/3.1/3.2/3.3 session, stream, forward, accept and raw modes;
- I2CP HostLookup types 2/3/4 and BlindingInfo type 42.

Record each result with the case ID from `FIXTURES.md`, the build commit, the
router versions, the netid and the exact capture file. A run without those is
not reproducible evidence.

## Known gaps exposed by the checked-in vectors

These are real divergences or unverified areas; the fixtures do not hide them:

| Case | Divergence |
|---|---|
| `i2np-header-bad-checksum` | openi2pd does not verify the 1-byte I2NP checksum on receive; Java rejects a bad checksum. This is not used by the NTCP2/SSU2 9-byte header paths, so its wire relevance is limited to legacy/full-header I2NP paths. |

MetaLeaseSet, DLM `0x12`, DSRM sender binding, tunnel-build windows, floodfill
RouterInfo freshness, SAM 3.2 framing, I2CP BlindingInfo, Datagram3 first
contact and Datagram2 offline emission now have static C++ implementations; they
still need Java-generated vectors and dynamic runs. Their fixtures are listed as
**Capture** or **Dynamic** in `FIXTURES.md`; none is claimed as passing.

## Safety rules

- Use a private loopback namespace/container or two disposable VMs.
- Do not reseed from, publish to, or connect to the public I2P network.
- Do not copy the Java tree into this repository or package it.
- Keep router data directories and Java configuration outside the checkout.
- Treat Java's accidental behavior as evidence, not as a specification; cite
  the specification before changing a wire format.
- Never accept invalid signatures, AEAD tags, lengths, or peer identities to
  make a test pass.
- Do not commit private keys, live router identities, session tags or
  unredacted public-network captures.

## What remains for the user's build environment

1. Build openi2pd with `-DBUILD_TESTING=ON` and run the existing unit/malformed
   targets (not executed here).
2. Build the optional Clang fuzz targets and run them with a time limit.
3. Write or supply `test-interop-vectors` implementing the contract in
   `vectors/README.md`; then run `check-vectors.pl --binary ...`.
4. Build the Java reference outside the checkout with its own tooling.
5. Capture the **Capture** fixtures in `FIXTURES.md` with throw-away keys.
6. Execute the loopback matrix and record results; update
   `docs/INTEROPERABILITY.md` from the coordinator side.

Until those steps are done, every dynamic result is **not executed** and no
compatibility claim is validated by this directory.
