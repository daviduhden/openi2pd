<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd cryptography boundaries.
-->
# Cryptographic boundaries and review map

Use with `SKILL.md`. This is a static trust map, not an audit conclusion.

| Area | Files | Trust/validation rule | Tests |
|---|---|---|---|
| Randomness | `libi2pd/Random.*` | CSPRNG only; no fallback to time/rand | crypto tests |
| Hashing/MAC | `Crypto.*`, `Siphash.h` | exact lengths, no truncated acceptance | `test-crypto-*`, `test-siphash` |
| Signatures | `Signature.*`, `Identity.*`, `Ed25519.*` | algorithm/key-length binding, exact transcript, constant-time verify | `test-signature-*`, `test-crypto-wire-validation` |
| RSA/SU3 reseed | `Reseed.*`, `Identity.*` | verify by default, bounded certificate/key parsing, no unsigned install | `test-reseed-zip-bounds` |
| Blinding | `Blinding.*`, `Destination.*`, `LeaseSet.*` | b33 strictness, derivation binding, expiry/identity checks | `test-blinding-*` |
| Elligator/GOST | `Elligator.*`, `Gost.*` | exact encodings, no unsupported fallback | `test-elligator`, `test-gost-*` |
| ECIES ratchet | `ECIESX25519AEADRatchetSession.*`, `Tag.h` | replay/tag budgets, NS/NSR validation before state change, key wiping | `test-eciesx25519-*` |
| PQ | `PostQuantum.*`, `Identity.*`, transport negotiation | runtime provider probe, supported type only, fail closed | ML-KEM/PQ tests, provider matrix |
| Torrent comments/identity | `TorrentsComments.*` | labels are pseudonyms; do not claim authentication | `test-torrents-comments` |

## Fail-closed rules

- Unsupported critical algorithm/version: reject.
- Missing trust root/certificate: reject.
- Verification error: reject; do not retry with a weaker algorithm.
- Provider lacks PQ support: do not advertise PQ; use a supported path only.
- Invalid AEAD tag/signature: no plaintext/state release.
- Ambiguous key ownership: do not guess; return an error.

## Key and secret checklist

- Are keys generated only through the project CSPRNG?
- Is the private key copied into a longer-lived buffer unnecessarily?
- Is it wiped on success, failure and exception/early return?
- Is the key ever logged, formatted into an error or sent over IPC?
- Is the public key length checked against the algorithm?
- Is the signature scope the exact required transcript/domain?
- Is nonce/tag ownership unique per key and session?
- Is a ratchet advanced only after successful validation?

## Test preparation

Add a known-answer or malformed case for every changed acceptance boundary.
Prefer deterministic vectors with provenance. Do not derive expected output by
running the code under test locally. Use a specification or independently
generated reference vector.

CI lanes:

- Linux Clang/GCC build + CTest (`security`, `i2p`).
- OpenSSL and LibreSSL builds, including no-PQ behavior.
- ASan/UBSan and TSan for lifetime/concurrency changes.
- Fuzzers for DER, protocol bounds and torrent parsers.
- Android JNI only when the Android graph links the changed code.

No local crypto execution, key generation or test run is allowed.
