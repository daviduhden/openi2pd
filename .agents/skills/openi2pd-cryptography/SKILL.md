---
name: openi2pd-cryptography
description: Use when changing or reviewing openi2pd cryptographic primitives, identities, signatures, blinding, ratchets, post-quantum selection, randomness or reseed verification.
---
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
  `TLS_method()`; PQ publication/selection stays runtime-gated.
- NSR timer lifecycle, provider PQ transcripts, LibreSSL/OpenSSL 4 builds and
  ASan/TSan crypto runs remain pending manual CI.

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
  `fuzz-protocol-bounds`, `fuzz-torrents-parsers`); add a target only for a new
  parser surface.
- Identify OpenSSL, LibreSSL/no-PQ, PQ-enabled, ASan/UBSan and TSan lanes.
- Record any provider-specific behavior as pending, not passed, until the
  corresponding manual job runs.

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
