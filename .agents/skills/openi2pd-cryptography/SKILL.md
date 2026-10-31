---
name: openi2pd-cryptography
description: Use when changing or reviewing openi2pd cryptographic primitives, identities, signatures, blinding, ratchets, post-quantum selection, randomness, reseed verification or OpenSSL 4.x/LibreSSL provider floors, excluding wire-parity claims owned by i2p-interop.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.
-->

# openi2pd cryptography

## Activate when

Use this skill for key handling, signature verification, AEAD/ratchet state,
identity encoding, blinding, post-quantum negotiation, reseed trust or any
change that can affect confidentiality, authenticity or key lifetime. Combine
it with `openi2pd-i2p-interop` for wire behavior and
`openi2pd-memory-security` for untrusted input or lifetime review.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. Cryptographic weakening
is never an acceptable workaround.

## Inspect first

- The primitive wrapper and the exact OpenSSL/LibreSSL API used.
- All callers that make trust decisions: signature verification, AEAD open,
  key derivation, reseed, identity parsing and PQ negotiation.
- Key/certificate length checks, algorithm identifiers and failure paths.
- The current crypto and identity rows in
  [`../../../docs/INTEROPERABILITY.md`](../../../docs/INTEROPERABILITY.md) and
  [`../../../docs/FEATURE-PARITY.md`](../../../docs/FEATURE-PARITY.md).
- The threat model at
  [`../../../docs/security/threat-model.md`](../../../docs/security/threat-model.md).

## Reference sources

- [`../../../docs/security/threat-model.md`](../../../docs/security/threat-model.md)
  — assets, adversaries and key-flow constraints.
- [`../../../docs/security/memory-safety-2026.md`](../../../docs/security/memory-safety-2026.md)
  — crypto/ratchet findings and residual risks.
- [`../../../docs/INTEROPERABILITY.md`](../../../docs/INTEROPERABILITY.md) —
  Java cryptographic behavior and known divergences.
- [`references/crypto-boundaries.md`](references/crypto-boundaries.md) — key
  surfaces, fail-closed rules and test map.

## Reconnaissance

Launch at least one read-only `explore` subagent before editing for the primitive
and another for trust decisions/callers. If subagents are unavailable, report
that limitation; do not substitute local execution. Ask for:

1. key/certificate parsing and length assumptions;
2. every verify/decrypt success path and its fallback behavior;
3. algorithm/version negotiation and provider capability checks;
4. secret lifetime, copying and wiping;
5. test vectors, fuzzers and Java references already present.

Do not let a reconnaissance agent run a crypto test, generate a vector by
executing project code, or inspect a real private key.

## Parallel implementation

- Split by independent primitive family (signature, ratchet, blinding, reseed,
  PQ) but not by editing the same shared key/identity type concurrently.
- One owner updates the Java compatibility row and test vectors for a change.
- Shared interfaces such as `CryptoKey`, `Identity`, `Signature` and
  `ECIESX25519AEADRatchetSession` require coordinator review.
- Keep a security regression alongside the fix; do not defer it to a later
  “audit” task.

## Conventions

- Apply the [source documentation policy](../../../AGENTS.md#source-documentation-policy)
  to key ownership, authentication failures and wire-size invariants.

- Use the existing OpenSSL/LibreSSL wrappers and project primitives; do not
  invent a bespoke construction or hand-rolled constant-time code.
- Fail closed for unsupported algorithms, malformed keys, failed verification,
  missing PQ support and missing trust roots. No “best effort” acceptance.
- Validate exact key, signature, nonce and tag lengths before use.
- Preserve domain separation, transcript binding, replay windows and nonce
  ownership. Never reuse a key/nonce or advance a ratchet before validation.
- Gate post-quantum advertisement on an actual runtime provider probe; do not
  advertise a type that cannot be used.
- Verify SU3/RSA/Ed25519 reseed paths by default; no unauthenticated reseed
  fallback.
- Wipe secrets with existing helpers and never log key material or plaintext.
- Document a deliberate incompatibility; do not silently normalize it.

## Known issues and decisions

- Signing-key wire sizes are one compile-time table in `Identity.cpp`
  (`public/private/signature`); unknown types return `false`, and callers keep
  their documented fallback. RSA-4096 excess signing data is bounded.
- `PrivateKeys::FromBuffer` uses a `ScopeExit` reset guard and dismisses it only
  on final success. Do not chain an already-offline signing key.
- Supported crypto floors are OpenSSL 3.0.2–4.0.x and LibreSSL 4.0+ with
  `TLS_method()`; CMake enforces only the **lower** floor (3.0.2 / 4.0), so
  "–4.0.x" is a support statement, not a configure guard. The probe cache is
  stamped by `I2PD_OPENSSL_EXPECT`/`I2PD_OPENSSL_EXPECT_VERSION` and
  `I2PD_OPENSSL_PROBE_STAMP` (include dir|kind|version); clear the build
  directory when switching backend or prefix. PQ publication/selection stays
  runtime-gated by the real provider round trip.
- HKDF prefers `EVP_KDF_fetch("HKDF")` with `OSSL_KDF_PARAM_*` when the CMake
  probe succeeds and keeps the `EVP_PKEY_HKDF` bridge for LibreSSL. Preserve
  the exact expand-only PRK derivation for zero-length keys.
- CMake stamps all `I2PD_HAVE_*` probe answers with the selected backend and
  runs one linked modern-API probe; reusing a build directory across
  OpenSSL/LibreSSL or prefixes must clear stale answers. Both reseed hostname
  setters are probed and a backend without either fails configure.
- SipHash provider output is pinned to 16 bytes with `OSSL_MAC_PARAM_SIZE`;
  the capability probe must sign, not only create a key. ML-KEM availability
  requires a real encapsulate/decapsulate round trip. `InitCrypto` returns
  false and the daemon refuses to start when a required provider algorithm
  (including Ed25519) is absent.
- ECIES/ElGamal encryptors return failure and cleanse key/IV/plaintext buffers.
  DSA/ECDSA DER buffers are sized by `i2d_*`; RSA rejects a modulus that does
  not fill the wire field. Reserved EC LeaseSet2 types (1/2/3) are never
  selectable and hybrid types require `IsMLKEMSupported`.
- `Signer::Sign` is still `void`; publication of an all-zero signature on a
  provider without DSA remains a planned fallible-interface change
  (`CRY-236`), not a fixed path. The I2PControl certificate/key pair is
  verified before use and TLS contexts set an explicit 1.2 floor.
- NSR timer lifecycle, provider PQ transcripts, Java type-8/blinding/NTCP2
  `pq` vectors, restricted-provider lanes, LibreSSL/OpenSSL 4 builds and
  ASan/UBSan/TSan crypto runs remain pending manual CI. `test-x25519-rfc7748`
  is the mandatory provider-independent X25519 KAT.

- SSU2 PQ version fallback must survive a zero-token version-rejection Retry:
  reset Noise/KEM/ephemeral state and request a fresh token; otherwise the
  downgrade path is unreachable. NTCP2's hybrid Noise names are
  `..._25519+MLKEM{512,768,1024}_...` (SSU2 uses `XKhfschaobfse`); a mix-up is
  silent, so keep the name/KDF guards.

- Ratchet New Session Reply splits are pending, not overwriting: keep up to
  eight `(receiveTagset, sendTagset)` pairs and promote the outbound split when
  the matching inbound tagset first consumes a tag (Java `firstTagConsumed`).
  A delayed NSR must never rewind an established tagset. Tag exhaustion marks
  the session send-exhausted but readable; the next send opens a new NS.
- New Session Reply allows only DateTime/Garlic/Options/Padding; Termination,
  NEXT_KEY, SessionID, ACK and unknown blocks are rejected before side effects.
  `crypto.ratchet.inboundTags` is capped at 320; the anonymous one-time NS
  path is gone (router-N is `32+payload+16`).
- Address-book subscription actions are signed protocol commands under I2P
  Proposal 112, not trusted merely because a feed URL owns the entry. The
  current native `action=delete` path checks source origin but does not verify
  `sig`/`oldsig`; before implementing signed actions, verify canonical
  preimages and old-destination authority against the proposal and pinned Java
  vectors, then retain the local cross-feed ownership restriction.
- X25519 key generation is fallible end-to-end: never persist, publish or pool
  an all-zero private/public pair; `X25519Keys` is non-copyable and derives the
  public key. Ed25519 secret scalars and nonces use `BN_clear_free`, the
  hand-rolled verifier rejects `S >= L`, and off-curve decoding fails closed.
- ML-KEM availability requires a real round trip on every backend (including
  LibreSSL); hybrid Noise initialisers return bool. An SSU2 Retry authenticated
  only with the public intro key may not change version unless the signed
  RouterInfo advertises that exact maximum; NTCP2 never randomly downgrades a
  PQ peer.
- The provider `HKDFContext` is stateful and not reentrant. Tag generation uses
  a per-thread instance (`GetThreadLocalHKDFContext()`); never serialise one
  context across the router, client and transport threads.
- Tunnel build-record encryption is fallible: no ephemeral key, a failed X25519
  agreement or an AEAD error must cleanse the shared secret, zero the whole
  record and fail the build. Never publish a record whose key material was
  produced by a partially failed operation.


- OpenSSL 4 deprecates `X509_NAME_get_text_by_NID`. Use the bounded
  `i2p::crypto::GetX509NameTextByNID` helper (index/entry/`ASN1_STRING_to_UTF8`)
  instead; it preserves the common-name contract, NUL-terminates and rejects
  conversion failure. Do not reintroduce the deprecated call in Family/Reseed.


- On OpenSSL 4/LibreSSL, `X509_NAME_get_entry` and
  `X509_NAME_ENTRY_get_data` return const pointers. Keep the bounded
  `GetX509NameTextByNID` helper const-correct; `ASN1_STRING_to_UTF8` already
  takes a const ASN1 string.

## Do not

- Do not compile, run CTest, sanitizers, fuzzers, Java peers or validators
  locally.
- Do not disable signature/AEAD verification, lower key sizes, reuse nonces,
  accept unknown critical fields or add a cleartext fallback.
- Do not add a new cryptographic dependency or runtime download without an
  approved task, license review and CI build lane.
- Do not copy Java/GPL implementation text; implement from the specification.
- Do not create commits, tags, releases, worktrees or temporary commits, and do not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies, fetch/vendor code or perform hidden downloads to enable a local build or test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Add known-answer tests and malformed-key/signature cases under `tests/`;
  register with `security` (and `i2p` where applicable).
- Use existing fuzzers where possible (`fuzz-signature-der`,
  `fuzz-protocol-bounds`, `fuzz-base-blinding`, `fuzz-ntcp2-blocks`,
  `fuzz-i2np-policy`, `fuzz-torrents-parsers`); add a target only for a new
  parser surface, with a checked-in corpus seed and per-target `-max_len`.
- Identify OpenSSL, LibreSSL/no-PQ, PQ-enabled, ASan+UBSan, UBSan-only and
  TSan lanes.
- Record any provider-specific behavior as pending, not passed, until the
  corresponding manual job runs.

- NTCP2's SipHash tag is 8 bytes while the peer-ordering digest is 16;
  both must use `OSSL_MAC_PARAM_SIZE` through `EVP_PKEY_CTX_set_params`, not
  the generic `EVP_PKEY_CTX_ctrl` digest-size control, or provider-backed
  OpenSSL can reject the session.
- Translation units that test `#if OPENSSL_PQ` must include `Crypto.h` (or
  `PostQuantum.h`) directly; relying on `Identity.h`/`Signature.h` to define
  the macro silently selects the non-PQ branch if the include chain changes.

## Tracking

Use `CRY`/C or the owning transport/client area. Record the primitive, trust
decision, test vectors, provider matrix and residual risk. Keep `PARITY-*` and
`NET-*` rows consistent. Propose a dated `PROGRESS.md` entry and any `TODO.md`/`ROADMAP.md` changes to the coordinator; append them only when explicitly assigned; subagents propose
tracking changes to the coordinator.

Record the exact AGENTS.md `Tracking states` value and state which validations were not run; do not collapse them into “pending”.

## References

- [Cryptographic boundaries](references/crypto-boundaries.md)
- [Threat model](../../../docs/security/threat-model.md)
- [Interoperability audit](../../../docs/INTEROPERABILITY.md)
- [Global policy](../../../AGENTS.md)
