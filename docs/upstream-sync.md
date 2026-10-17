# Upstream provenance and synchronization

## Imported sources

| Project | URL | Branch | Commit | Imported into |
|---|---|---|---|---|
| Documentation | https://github.com/PurpleI2P/i2pd_docs_en | default (`master`) | `c5d86a8d177c7215d710e6b2370c6f128cf7cfc0` | `docs/` (upstream files; fork reports coexist) |
| Tools | https://github.com/PurpleI2P/i2pd-tools | default (`master`) | `ca7ece8da86aa80b58fce6fa64375bfd75cbf843` | `tools/` |
| Router source | https://github.com/PurpleI2P/i2pd | upstream release corresponding to version 2.61.0 | see fork history | existing main tree; not re-imported |

The imported documentation has no HTML pages that needed conversion: all 45
upstream documentation files are Markdown (plus images). The tool sources had
no HTML fixtures. File lists and the manual comparison script are in
`contrib/upstream/`.

## Imported-file rules

- Upstream license and copyright notices are preserved:
  `docs/LICENSE.i2pd-docs-en`, `docs/about/license.md`,
  `tools/LICENSE`, and copyright headers in source files.
- The upstream `mkdocs.yml` was adapted to `docs_dir: .`, the fork name and the
  integrated navigation; MkDocs is optional and is not a build/runtime
  dependency.
- `tools/Makefile`, `tools/CMakeLists.txt`, `dependencies.sh`,
  `.gitmodules` and the `i2pd` submodule directory were **not** imported.
  `tools/CMakeLists.txt` is a new in-tree build definition.
- Tool sources were adapted to C++23 and the in-tree libraries. Functional
  changes are listed in `security-audit.md` and `files-changed.md`.

## Reproducible comparison

```sh
# Documentation only
contrib/upstream/sync.sh --docs

# Tools only
contrib/upstream/sync.sh --tools

# Both
contrib/upstream/sync.sh --all
```

The script:

1. Clones the exact pinned commits into a temporary directory.
2. Prints a unified diff for every file listed in
   `contrib/upstream/docs-files.txt`, `docs-extra-files.txt` and
   `tools-files.txt`.
3. Removes the temporary directory on exit.
4. **Never writes to the repository.**

Review each diff manually. Upstream changes that conflict with fork-specific
requirements must be rejected or adapted. In particular, never reintroduce:

- GNU make files or submodule instructions;
- the HTTP administrative console or `[http]` options;
- C++17/C++20-only build paths;
- hidden downloads during configure/build;
- external `i2pd` copies instead of the in-tree libraries.

## After a manual sync

1. Update the commit hashes in this document and in
   `contrib/upstream/sync.sh`.
2. Re-apply C++23, CMake target, TUI and security changes.
3. Update `docs/files-changed.md` and `docs/attribution-audit.md`.
4. Rebuild and run `ctest`. Automatic CI remains `workflow_dispatch` only.
