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

- Compatibility signing's challenge becomes secret-dependent after scalar
  multiplication, even though its final signature is public. Give challenge
  and private scalar clear-free owners across every arithmetic failure;
  retain ordinary cleanup for verification's genuinely public challenge.
  Scope negative source guards to Sign so they do not reject Verify.

- Signature constructor lengths are protocol geometry, not trusted buffer
  capacities. Validate ECDSA curve/coordinate pairs and RSA modulus widths
  before allocation, narrowing or failure clearing. Invalid parameters grant
  no writable extent; keep tiny-buffer negative controls in both backends.
- A checked point recovery helper changes every caller's failure contract,
  including backend initialization. Decode into owned coordinates and check
  each arithmetic result before use; balance context frames on every return.
  Point copies must not retain partial coordinates on allocation failure.
  These boundaries do not establish allocation safety for backend constant
  setup, table initialization or Sum/Double/MulB.

- Ed25519 compatibility must compare the supplied public key again after
  the historical bit-253 scalar adjustment. An arbitrary mismatch is not
  evidence of a legacy key. Publish only an IsValid signer; reject invalid
  online serialized pairs and retain modern/RFC plus genuine historical
  controls. Ed25519ph needs a provider key and must not report a legacy-only
  fallback as ready. Full Sign failure propagation remains separate.
- Private-key expansion returns checked success. Digest failure clears all
  64 output bytes and must abort both compatibility signing and private-key
  blinding; never clamp an unavailable digest into a usable scalar. Keep
  every caller synchronized with that fallible API.
- Public-point encoding also returns checked success and clears failed
  output. Normalize must validate context/coordinates and all allocation,
  inversion and multiplication outcomes before encoding. Compatibility and
  RedDSA constructors publish readiness only after encoding; key factories
  and random-key wrappers must honor it. MulB/Sum/Dbl allocation handling
  remains outside that guarantee, as does full Sign failure propagation.
- When adding an override in a signature family, review all sibling methods
  and both provider branches. Mixed annotations can produce one diagnostic
  per translation unit. Use compile-time bounded int components/digests at
  OpenSSL length boundaries; preserve size_t for buffer-capacity APIs.

- Retry payload grammar must validate before changing version-bound crypto
  state as well as before inserting its token. The first DateTime block
  alone cannot authorize a malformed or forbidden trailing block; test a
  valid prefix followed by each disallowed block in the Retry state.

- Retry header unmasking is not payload validation. Publish its outgoing
  token only after AEAD, full block validation and termination handling;
  rejecting a packet must leave the token cache unchanged. Audit NewToken,
  HolePunch and RelayResponse update sites with their own enclosing payload
  and signature checks rather than treating every token field alike.

- Inspect nested framing randomness before publishing tunnel correlation,
  not only explicit key/tag fills. Complete wrapping first, cleanse both
  secrets on every exit, and retire an unsent attempt by identity outside
  its correlation lock when construction throws.
- Handshake resend buffers are transactional output. Revoke them before
  provider work in local builders, but preserve the previous valid attempt
  when unauthenticated input is rejected. A validated Retry/version change
  retires old wire state before initializing its replacement.
- Router key candidates must be cleansed if entropy or persistence fails.
  Supported identity generation failures must not retry another algorithm;
  keep explicitly requested legacy types separate from failure handling.
- A checked provider wrapper is insufficient if its caller ignores status.
  Review Tag::Randomize and key-generation consumers as well as RAND calls;
  never submit a zeroed failure buffer as a new lookup identity.

- GetRngSeed publishes a 32-bit provider result, including zero, or throws
  on provider failure. Never replace failed randomness with clocks or a
  fixed value. Its per-call constrained byte-source adapter permits
  deterministic boundary/failure tests without changing global providers.
  Review constructor and executor exception boundaries when adding callers:
  global initialization can terminate before main, and this API does not
  promise graceful recovery. Provider-diversity tests must allow legitimate
  32-bit collisions instead of requiring every sample to be unique.
- Random.h owns checked byte filling for callers that abort by exception:
  FillRandomBytes handles the provider's signed byte-count bound and skips
  empty spans, cleansing the entire destination before propagating provider
  failure. Its per-call source seam tests partial writes without modifying
  global providers. RandomUInt32 propagates failure rather than returning
  zero; otherwise RandomBelow can spin forever rejecting a failed word for
  a non-power-of-two bound. Keep valid zero and failure as separate cases,
  and test both rejection sampling and first-attempt failure propagation.

- Secret-cleansing source contracts must follow the owning RAII deleter
  when a raw BN_clear_free call is removed. Pin both the secret BIGNUM
  owner type and its BN_clear_free construction rather than requiring a
  legacy manual-free spelling or accepting an unrelated deleter elsewhere.

- Ed25519 compatibility signing reduces all 64 SHA-512 nonce bytes modulo
  L before encoding the 32-byte scalar consumed by MulB. Truncation can
  produce valid signatures but fails RFC 8032 deterministic vectors and
  differs from provider signing. Keep the strict vector comparison for
  direct and compatibility calls, including overlapping message/output.
  This correction changes historical compatibility signatures for the same
  key/message, while preserving R/S encoding and verification compatibility.
  SignRedDSA already uses full-width reduction; do not replace its random
  nonce transcript with Ed25519's deterministic transcript.
  Both nonce reductions set BN_FLG_CONSTTIME to request the library's
  protected division path. This mitigation does not establish constant-time
  signing: the complete fallback scalar/point timing surface still needs
  review across OpenSSL and LibreSSL and independent timing validation.

- Ed25519 key and signature lengths are namespace-scoped constants in
  `i2p::crypto`, not preprocessor macros. Tests outside that namespace must
  qualify array extents unless a local using declaration supplies lookup.
  A failed extent can cascade into undeclared output buffers; fix the
  first constant lookup without changing buffer sizes or weakening the
  negative crypto assertions. A sanitizer compile failure supplies no
  runtime evidence for the skipped regressions.

- Pending ratchet reply correlation borrows its receive tagset; the inbound
  tag map retains the session through that tagset. Making both edges strong
  creates a session/tagset cycle even after destination teardown. Lock weak
  references before use, retain successful locks outside the pending mutex,
  and preserve inbound authority plus lost-reply promotion regressions.
  The teardown regression is not attribution of historical RT-031 leaks.

- Family certificate loading has a related filename-count pitfall: count
  successful map insertions, not attempted files. Do not copy reseed map
  replacement into Families without invalidating RouterInfo's cached
  FamilyID values; ID reuse could associate a router with another family.
  Family trust revocation needs a coordinated identity/cache design.

- Reseeder trust reload revokes the previous map before reading the
  directory and publishes a fresh map of usable signer identities.
  Malformed files do not count, and an unreadable directory must not
  preserve removed anchors. Load/import remain serialized by the owner;
  do not claim concurrent map replacement. NetDb normally constructs a
  new Reseeder for each job, so distinguish repeated explicit reload
  behavior from process-wide trust retention when documenting findings.

- RedDSA signing checks both SHA-512 transcripts and their exact lengths,
  nonce generation, scalar decoding/reduction and scalar arithmetic before
  committing a signature. Scoped cleanup wipes nonce/digest buffers and
  clear-frees all scalar intermediates; failure zeroes the output without
  clobbering an overlapping message before hashing finishes. Preserve positive
  and overlapping-buffer controls alongside missing-input regressions.
  This wrapper does not make shared MulB/Sum/Dbl or early initialization
  allocator-failure safe: those unchecked point-arithmetic seams require a
  separate fallible-interface refactor and provider/allocation fault tests.

- Reference digest helpers should return fixed-size arrays for their
  algorithm. The EdDSA fixture formerly passed a 32-byte output to SHA-512
  and then to the 64-byte scalar decoder. Review both provider writes and
  verifier reads when an ASan address lies near an adjacent stack object;
  that object need not own the undersized buffer. Keep cross-platform
  assertions open until CI reruns the corrected fixture.

- Destination lookup reply keys/tags and publication tokens must check
  `RAND_bytes` before registration or transmission. Lookup failure returns
  through the existing completion path; publication failure schedules its
  existing retry. Cleanse the temporary reply key on every exit, including
  provider failure. When changing callback captures in the same scope,
  reuse one weak owner declaration to avoid duplicate local declarations.
- OpenSSL `BN_CTX_get()` is valid only inside a `BN_CTX_start()`/`BN_CTX_end()`
  frame. The GOST RFC 7091 edge-case fixture omitted that frame and failed on
  OpenBSD/LibreSSL; repository-wide search found no other direct test-side
  `BN_CTX_get()` users. Keep temporary BIGNUM frames balanced before freeing
  their context (`TF-057`, pending manual CI).
- `Tag`/`IdentHash` expose only a const `data()`, while OpenSSL
  `SHA256`/`SHA1`/`SHA512` write through their third argument. Hash into a
  mutable array (or pass the object, whose non-const conversion exists) and
  compare with `std::memcmp`; `actual.data()` is ill-formed (CE-070).
- RouterInfo and LS1 carry exactly one signature whose length is the
  identity's. An offline/transient signer of another type must be refused,
  not written into a slot sized for the primary key (RT-022). Keep
  `Signer::Sign` fallibility open (CRY-236).

- An all-zero X25519 scalar is rejected by the constructor and
  `SetPrivateKey`; it is not a secret. `CreateECIESX25519AEADRatchetRandomKeys`
  wipes every non-null output buffer when it fails, so tests must pass scratch
  buffers or they silently build a key from zeros.
- A router-N ratchet payload is AEAD-protected: a test that inspects inner
  blocks must mirror `RouterIncomingRatchetSession` (InitNoiseNState, MixHash
  of the ephemeral key, MixKey of the X25519 agreement, Decrypt) before
  parsing. Parsing ciphertext as `[type][size]` headers cannot be valid.

- Signing-key wire sizes are one compile-time table in `Identity.cpp`
  (`public/private/signature`); unknown types return `false`, and callers keep
  their documented fallback. RSA-4096 excess signing data is bounded.
- `PrivateKeys::FromBuffer` uses a `ScopeExit` reset guard and dismisses it only
  on final success. Do not chain an already-offline signing key.
- LeaseSet2 DH/PSK producer and reader paths use scoped cleansing for
  `authInput`, HKDF `okm`, and the producer's `authSalt`. This does not cover
  copied client-key containers or serialized buffers; keep those separate in
  CRY-237 and review every early return when extending these paths.
- The current published validation run reported an EdDSA verifier stack
  underflow with unsymbolized frames. Preserve it as unresolved until exact
  symbols identify the read; valid-signature verification is a positive
  control and must remain covered under ASan.
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

- An AEAD result gates the send: SSU2 PeerTest/HolePunch/TokenRequest/Retry
  must not increment a length, transmit or charge bytes after encryption
  failure. Pass the real fixed-buffer capacity to the helper. Bound the SSU2
  frame before sizing any stack buffer from it.
- X25519 is mandatory for NTCP2, SSU2, ElGamal transport exchange and the
  ratchet. Probe it with a keygen/raw-public-key round trip and require it in
  `InitCrypto`; remove a capability field that no caller reads. Ed25519 and
  RedDSA encoders are part of the success flag and zero the complete output
  on failure.
- The NTCP2 AEAD nonce is little-endian with a zeroed 4-byte prefix; the
  spec and `htole64buf` agree. Do not "correct" it to big-endian prose.

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
