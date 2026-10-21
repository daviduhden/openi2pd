<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: README.

Documents regression and interoperability checks.

-->

# Checked-in deterministic vectors

This corpus retains its I2P Java 2.13.0 provenance at commit
`9134f808337b401e8e53c73734c81fab04280c9d`. The current compatibility target
is 2.14.0, checked separately; these bytes and Java line citations have not
been re-derived for it. `--reference` and `I2P_JAVA_FIXTURE_REFERENCE` select
the historical corpus checkout, not the current target.

Every `*.case.json` describes one input and the normalized parse that a C++
harness must produce. Inputs are either:

- `*.hex` — lowercase/uppercase hex; whitespace and lines beginning with `#`
  are ignored. The `#` lines are provenance comments, not data.
- `*.txt` — exact raw bytes (used for line-oriented SAM handshakes).

`valid/` contains structurally valid inputs (some expect a negative protocol
reply, for example `HELLO VERSION ... NOVERSION`). `malformed/` contains
inputs that the Java reference rejects.

## Case schema

```json
{
  "id": "unique-case-id",
  "kind": "i2np | i2cp | sam",
  "input": "file.hex",
  "format": "hex | raw",
  "sha256": "<sha256 of the decoded input bytes>",
  "expectation": "accept | reject",
  "license": "BSD-3-Clause",
  "provenance": {
    "spec": ["i2np#database-lookup"],
    "java": [
      { "path": "router/java/.../X.java", "line": 123, "contains": "token" }
    ]
  },
  "expect": { "ok": true, "value": { } },
  "known_gap": "optional: current openi2pd divergence",
  "note": "optional"
}
```

`expect` is the **normalized parse**, not a captured Java stdout dump. It is
derived from the field layout in the cited Java source and the public
specification. The static validator independently checks:

- the SHA-256 of the decoded input;
- that non-truncated I2NP inputs have a matching declared size and a checksum
  equal to the first byte of `SHA-256(payload)`;
- that non-truncated I2CP inputs have a matching 4-byte length;
- that every cited Java file/line/`contains` triple still matches the pinned
  checkout when `--reference` is supplied.

It does **not** claim that the C++ implementation agrees. That comparison is
performed only when the user supplies `--binary`.

## Expected parse shapes

I2NP `DatabaseLookup` (`kind: i2np`):

```json
{
  "ok": true,
  "value": {
    "type": 2,
    "message_id": 287454020,
    "expiration": 1700000000000,
    "size": 67,
    "checksum_valid": true,
    "database_lookup": {
      "search_key": "<64 hex>",
      "from_hash": "<64 hex>",
      "flags": 0,
      "lookup_type": "ANY | LS | RI | EXPL",
      "reply_tunnel": null,
      "excluded_peers": [],
      "reply": null
    }
  }
}
```

Reply variants:

- ElGamal (`0x02`): `{"kind":"elgamal","key":"<64 hex>","num_tags":1,"tag":"<64 hex>"}`
- ratchet tag (`0x10`): `{"kind":"ratchet_tag","key":"<64 hex>","num_tags":1,"tag":"<16 hex>"}`
- ratchet public key (`0x12`): `{"kind":"ratchet_pubkey","key":"<64 hex>"}`

I2NP `DatabaseSearchReply` (`kind: i2np`):

```json
{
  "ok": true,
  "value": {
    "type": 3,
    "message_id": 287454020,
    "expiration": 1700000000000,
    "size": 65,
    "checksum_valid": true,
    "database_search_reply": {
      "key": "<64 hex>",
      "from": "<64 hex>",
      "peers": []
    }
  }
}
```

I2CP (`kind: i2cp`): `value` contains `type`, `length` (payload length, not
including the 5-byte length/type header) and either `host_lookup` or
`blinding_info`.

SAM (`kind: sam`): `value.sam_reply` is the exact reply line including `\n`.
Malformed cases use `{"ok": false}`.

## Harness contract

The optional binary is invoked once per case:

```sh
"$BINARY" --case CASE_ID /absolute/path/to/input
```

It must exit 0 and print one JSON object to stdout. It must not read the
network, the clock, or the Java tree. Errors are reported by printing
`{"ok": false}` (or by a non-zero exit for a harness bug). The runner compares
JSON semantically (canonical key order and number spelling).

Suggested location for the future harness source is `tests/test-interop-vectors.cpp`
with target `test-interop-vectors`; it is intentionally not added or
registered here because it needs a small pure-parser adapter for I2CP/SAM that
does not exist yet.

## Determinism limits

- The fixed I2NP expiration is intentionally in the past. Vector mode is a
  parser test, not a delivery test; handlers that enforce wall-clock freshness
  must not be used.
- No signatures, session keys, tags or identities from a real router are
  present. These vectors do not test signature verification.
- `known_gap` marks a case where the checked-in expectation follows the Java
  source but openi2pd is known to differ. A differential run reports
  `KNOWN-GAP` instead of a pass until the implementation is fixed.

## Adding a case

1. Derive the bytes from the specification/Java layout; do not copy a random
   capture unless you have a provenance record.
2. Put valid and malformed inputs in the respective directory.
3. Add the case JSON with `sha256` and provenance.
4. Run:

   ```sh
   I2P_JAVA_FIXTURE_REFERENCE=/var/tmp/i2p-java-2.13.0 \
     tests/interop/check-vectors.pl
   ```

5. Never commit private keys, live router identities, session tags or
   unredacted packet captures from a public network.
