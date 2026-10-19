# Upstream provenance and synchronization

## Imported sources

| Project | URL | Branch | Commit | Imported into |
|---|---|---|---|---|
| Documentation | https://github.com/PurpleI2P/i2pd_docs_en | default (`master`) | `30ddc4c7c74e055fc858ecff3ae6f69d1a73ab5e` | `docs/` (upstream files; fork reports coexist) |
| Tools | https://github.com/PurpleI2P/i2pd-tools | default (`master`) | `ca7ece8da86aa80b58fce6fa64375bfd75cbf843` | `tools/` |
| Router source | https://github.com/PurpleI2P/i2pd | upstream release corresponding to version 2.61.0 | see fork history | existing main tree; not re-imported |

The pinned upstream documentation tree contains **46 files**: 39 Markdown
documents, the two branding images (`img/logo.png`, `img/favicon.ico`) and five
tutorial images, with **no `.html` or `.htm` files**. There is therefore no
HTML source to convert at this pin. The only inline HTML in the upstream tree
was the anchor form used by the torrent RPC reference; those anchors were
converted to Markdown heading attributes (`{#auth}`, `{#methods}`,
`{#torrent-fields}`, `{#peer-object}`, `{#file-object}`,
`{#file-stats-object}`, `{#tracker-entry}`, `{#tracker-stats-entry}` and
`{#units}`) and `attr_list` was added to the optional MkDocs configuration.

Coverage of the 46 upstream files is:

- **44 direct imports** under the same path;
- **2 renamed imports** for the nested BitTorrent guide and RPC reference:
  `tutorials/Filesharing/I2PD Built-in Torrent/` upstream maps to
  `tutorials/Filesharing/built-in-torrent/` locally. The mapping is recorded
  once in `contrib/upstream/docs-path-map.txt` and used by `sync.sh`;
- **74 fork-owned local paths** (61 Markdown pages plus 13 assets; C++23, IPC,
  TUI, torrent architecture, Android, CMake/POSIX make, Perl, OpenBSD, security,
  branding and reports) coexist in `docs/` but are not upstream imports.
  Together with the two renamed torrent pages they account for the 76
  local-only paths after the 44 direct imports.

The five tutorial diagrams under `tutorials/media/` are byte-identical to the
pinned upstream tree. `docs/img/logo.png` and `docs/img/favicon.ico` are
intentionally replaced by the official openi2pd assets; upstream branding is
not treated as the fork's identity. License text is preserved unchanged in
`LICENSE.i2pd-docs-en` and `about/license.md`.

The old flat `tutorials/Filesharing/built-inTorrent.md` page was removed; it
was a duplicate of the upstream nested guide. The current comparison page
`INTEROPERABILITY.md` replaces the earlier `interop-matrix.md`; the superseded
file was removed to avoid two competing matrices.

If a future upstream commit introduces HTML, convert it manually to Markdown
before importing, preserve headings, tables, diagrams/images, code blocks,
anchors and links, and record the source URL/commit in this document. Do not
add an HTML converter, JavaScript runtime or download step to the daemon build.

## Imported-file rules

- Upstream license and copyright notices are preserved:
  `docs/LICENSE.i2pd-docs-en`, `docs/about/license.md`,
  `tools/LICENSE`, and copyright headers in source files.
- Tool provenance, imported/local-only files and per-executable coverage are
  recorded in `docs/tools/provenance.md`; `tools/test-tools-inventory.cmake`
  checks the manifest and source inventory.
- Documentation provenance is split into three explicit inventories:
  `contrib/upstream/docs-files.txt` (upstream paths),
  `contrib/upstream/docs-path-map.txt` (renamed upstream paths) and
  `contrib/upstream/docs-extra-files.txt` (LICENSE/README/MkDocs). Fork-owned
  pages are not copied from upstream and must not be added to these manifests.
- The upstream `mkdocs.yml` was adapted to `docs_dir: .`, the fork name and the
  integrated navigation; MkDocs is optional for publishing static docs and is
  not a daemon, build or runtime dependency. It is not invoked by CMake, the
  router, the TUI or any test that the daemon needs to run.
- `tools/Makefile`, `tools/CMakeLists.txt`, `dependencies.sh`,
  `.gitmodules` and the `i2pd` submodule directory were **not** imported.
  `tools/CMakeLists.txt` is a new in-tree build definition.
- Tool sources were adapted to C++23 and the in-tree libraries. Functional
  changes are listed in `security-audit.md` and `files-changed.md`.
- Fork-specific user guides (C++23, multiprocess IPC, TUI, native BitTorrent,
  Android, CMake/POSIX make, Perl and OpenBSD) are maintained locally and are
  not overwritten by a bulk upstream copy.

## Fork documentation map

| Area | Local source of truth |
|---|---|
| C++23 and ownership | `docs/cpp23-migration.md`, `docs/devs/building/requirements.md` |
| Multiprocess IPC and privilege separation | `docs/security/privilege-separation.md`, `docs/security/ipc-protocol.md`, `docs/security/process-capabilities.md`, `docs/security/windows-worker-ipc.md` |
| TUI | `docs/tui-ipc.md`, `docs/TUI-WINDOWS.md`, `docs/user-guide/run.md`, `tui/` |
| BitTorrent | `docs/torrent/`, `docs/tutorials/Filesharing/built-in-torrent/`, `libi2pd_client/Torrents*` |
| Android | `docs/android-modernization.md`, `docs/android-java25.md`, `docs/android-jni-status.md`, `docs/devs/building/android.md`, `android/` |
| CMake and POSIX make | `docs/build-system.md`, `docs/dependencies.md`, `docs/devs/building/unix.md` |
| Perl | `docs/development/perl-migration.md`, `docs/tools/i2pdctl.md`, `tools/scripts/` |
| OpenBSD | `docs/security/openbsd-validation.md`, `contrib/openbsd/`, `privsep/OpenBSDSandbox.*` |
| Current Java interoperability | `docs/INTEROPERABILITY.md` (the legacy `interop-matrix.md` was removed as a duplicate) |

## Reproducible comparison

```sh
# Confirm whether the pinned upstream branch has moved (no tracked writes).
git ls-remote https://github.com/PurpleI2P/i2pd_docs_en HEAD refs/heads/master

# Documentation only: coverage checks plus read-only diffs.
contrib/upstream/sync.sh --docs

# Tools only
contrib/upstream/sync.sh --tools

# Both
contrib/upstream/sync.sh --all
```

The script:

1. Clones the exact pinned commits into a temporary directory.
2. Checks that every file in the upstream `docs/` tree is represented in
   `docs-files.txt`; reports any new upstream file as `UNTRACKED UPSTREAM`.
3. Rejects a pin that contains `.html`/`.htm` files until they are converted
   manually to Markdown.
4. Resolves the two renamed BitTorrent paths through
   `contrib/upstream/docs-path-map.txt` and verifies every local target exists.
5. Prints a unified diff for every file listed in `docs-files.txt`,
   `docs-extra-files.txt` and `tools-files.txt`.
6. Removes the temporary directory on exit.
7. **Never writes to the repository.** It exits non-zero for an unclassified
   upstream file, an HTML source or a missing local target; normal content
   differences are printed for manual review and do not fail the run.

Review each diff manually. Upstream changes that conflict with fork-specific
requirements must be rejected or adapted. In particular, never reintroduce:

- GNU make files or submodule instructions;
- the HTTP administrative console, `[http]` options or browser torrent UI;
- C++17/C++20-only build paths;
- hidden downloads during configure/build;
- external `i2pd` copies instead of the in-tree libraries.

### Link and content check

The documentation is Markdown; `docs/mkdocs.yml` is an optional publication
configuration. Before accepting a sync, check relative links, images and local
heading/anchor fragments with the offline checker, and review code examples
against the local tree and the real source (MkDocs itself is not required):

```sh
perl tools/scripts/check-doc-links.pl docs
perl tools/scripts/validate-branding.pl
git diff --check -- docs contrib/upstream
```

- `docs/about/contributing.md` and `docs/testing.md` describe the static checks.
- `docs/README.md` is the documentation index; `docs/developer-guide/README.md`
  is the developer entry point; `docs/mkdocs.yml` is publication-only.
- The upstream pages that mention the removed web console are adapted locally
  to the TUI and optional loopback RPC. Do not restore a browser UI.
- Torrent pages must match `libi2pd_client/TorrentsRPC.cpp` and
  `libi2pd_client/TorrentsTunnel.cpp`: POST + `application/json`, loopback-only
  RPC, no CORS/preflight, no static web route and no clearnet fallback.
  `docs/torrent/ipc.md` must match `daemon/TuiServerCommon.cpp` and
  `libi2pd/TuiProtocol.h`.
- TUI keys and views must match `tui/main.cpp` (`TAB_NAMES`, `HandleKey`):
  ten views, `1`-`9` plus `0` for Connections, `t` transit confirmation,
  `l` local log threshold, `L` daemon log level, `n` NetDb directory and
  `/` filtering.
- Android build instructions must match `android/app/build.gradle`,
  `android/gradle/wrapper/gradle-wrapper.properties`, the Android CMake graph
  and `android/tools/java25-probe/check-config.sh`.
- C++23, IPC, Android, CMake, Perl and OpenBSD guides remain fork-owned; merge
  only upstream technical corrections that still apply.
- `docs/user-guide/install.md` must list only supported platforms
  (Linux, OpenBSD, Windows, Android); macOS, FreeBSD and Solaris are explicitly
  unsupported by the CMake entry point and are not advertised as installable.
- `docs/devs/building/requirements.md`, `docs/build-system.md` and
  `docs/devs/building/unix.md` are the CMake/toolchain source of truth. Do not
  reintroduce GNU make, the removed `[http]` console options or upstream
  submodule instructions.

## After a manual sync

1. Update the commit hashes in this document and in
   `contrib/upstream/sync.sh`, then regenerate `docs-files.txt` from the pinned
   upstream tree. Do not add fork-owned pages to that manifest.
2. Add a row to `docs-path-map.txt` when an upstream path is renamed locally;
   remove stale rows. Run `contrib/upstream/sync.sh --docs` and require
   `Coverage OK`.
3. Re-apply C++23, CMake target, TUI, Android, BitTorrent, Perl and security
   changes. Keep the fork-specific pages listed above authoritative.
4. Remove duplicate upstream/locally adapted pages instead of retaining two
   sources of truth; update the MkDocs nav and every index link.
5. Run the relative-link/image check, branding validation and
   `git diff --check`. Review anchors after any heading rename.
6. Update `docs/files-changed.md`, `docs/attribution-audit.md`,
   `docs/android-modernization.md` (if Android changed), `ROADMAP.md`,
   `PROGRESS.md` and `TODO.md`.
7. Let manual CI perform configure/build/CTest and Markdown publication checks;
   automatic CI remains `workflow_dispatch` only. MkDocs is still optional and
   is not added to the daemon build or runtime.
