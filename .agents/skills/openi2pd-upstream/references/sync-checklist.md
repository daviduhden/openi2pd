<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd upstream synchronization checklist.
-->
# Upstream synchronization checklist

Use with `SKILL.md`. This is a review template, not an import script.

## Review record

For each upstream revision:

```text
Upstream repo/branch:
Commit / date:
Upstream files or behavior:
Local owner / files:
Disposition: applied | already present | adapted | omitted | deferred
Reason:
Wire/persisted-format impact:
License/provenance:
Tests prepared:
CI lane:
```

## Conflict screen

Reject or adapt when upstream:

- requires a C++ standard below the project floor or a compiler fallback;
- reintroduces GNU make, an HTTP/web console, browser assets or a submodule;
- adds a configure-time/runtime download or hidden checkout;
- assumes monolithic process/network/storage authority;
- changes release identity, version metadata or binary/data paths;
- copies code with incompatible licensing or missing provenance;
- weakens input bounds, signature/AEAD checks, `pledge`/`unveil`, sandboxing
  or credential checks;
- changes a wire/persisted format without a specification and migration.

## Local adaptation

1. Read the upstream behavior, not just the diff.
2. Find any existing local implementation and its tests.
3. Implement the smallest behavior change that fits the local architecture.
4. Preserve I2P compatibility and fork-specific interfaces.
5. Add a focused regression and update the disposition table.
6. Update provenance/manifests only when the imported file set changed.

## Manifests

`contrib/upstream/sync.sh` and the `.txt` manifests are comparison aids. A
review may use a coordinator-provided read-only checkout, but no agent may run
the script, clone into the repository or leave generated imports unrecorded.

## Verification

Static review of the disposition, docs links, source bounds and tests. Dynamic
validation remains the owning subsystem's manual CI lane. Do not describe an
upstream revision as applied-and-validated when only the source comparison is
complete.
