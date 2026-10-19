<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: upstream sync.

Documents project documentation.

-->

# Upstream provenance and synchronization

## Imported sources

| Project | URL | Branch | Commit | Imported into |
|---|---|---|---|---|
| Documentation | https://github.com/PurpleI2P/i2pd_docs_en | default (`master`) | `30ddc4c7c74e055fc858ecff3ae6f69d1a73ab5e` | `docs/` (upstream files; fork reports coexist) |
| Tools | https://github.com/PurpleI2P/i2pd-tools | default (`master`) | `ca7ece8da86aa80b58fce6fa64375bfd75cbf843` | `tools/` |
| Router source | https://github.com/PurpleI2P/i2pd | `openssl` | reviewed through `472ef88b0eb3fbc3eba5fe8e0b9336a9b8df4499` | existing main tree; selective adaptation, no bulk import |

## Reviewed router-source commits (2026-10-18/19 UTC)

The following commits were reviewed from `PurpleI2P/i2pd` branch `openssl`.
The window is expressed in UTC. Commits whose author date is 17 October in the
upstream `-04:00` timezone but whose UTC timestamp falls on 18 October are in
the 18 October group; commits whose author date is 18 October but whose UTC
timestamp falls on 19 October are in the 19 October group. `472ef88b` was the
branch tip at review time. This is a review record, not a merge base or a claim
that the fork is at upstream 3.0.0.

| Upstream commit | UTC date | Subject | Local result |
|---|---|---|---|
| `18340106ec7623544e0811192c2acd8439d5d626` | 2026-10-18T01:19:38Z | Use `stringstream::view` and check `getline` EOF | Already present through local `95abd377` in the request-side HTTP tunnel paths. |
| `475f40f8d925afc7519ffdfa59144a62b2a39017` | 2026-10-18T01:55:50Z | Use `view` in the server tunnel and count headers for the connection | Already present through local `95abd377`; the fork keeps the member header counter and 8192-byte limit. |
| `7dfc05c098cc62a4e3144d2bb54cf985c14c2c68` | 2026-10-18T13:38:10Z | Do not save a piece of a removed torrent | Adapted: weak capture plus an authoritative `TorrentsTunnel::FindTorrent` membership check at disk-job execution. A removal race after the check remains possible; a storage-generation tombstone is still needed for a strict guarantee. |
| `77ebf7516ea44cfe0960831c9161d20983f64130` | 2026-10-18T14:40:57Z | Do not save piece blocks for a stopped torrent | Applied through `Torrent::CanAcceptPieceBlock`, preserving the fork's exact offset/length and piece-mutex checks. |
| `9c3a742dc0ae8e5aa4c104683f604185fa3639cb` | 2026-10-18T14:46:15Z | Allow connections from the Debian 12 i2pd package | Applied: the general peer floor is 0.9.57; `NETDB_MIN_HIGHBANDWIDTH_VERSION` remains 0.9.62. The policy relaxation is documented and test-pinned. |
| `942d6907d2b2cfa73dceaf117457329d251f4191` | 2026-10-18T14:49:01Z | Display the torrents table after the first added torrent | Not applicable: upstream changes `contrib/torrents/web/main.js`; this fork deliberately removed the browser console. The native TUI already keeps the table visible and refreshes after commands. |
| `e3856a38fa0fe310726a39faa9df16f035c1bf69` | 2026-10-18T15:01:02Z | Use `stringstream::view` in the server response and IRC paths | Applied, including the response/IRC lifetime conversions and the related incomplete-header compaction fix. No standalone I2PTunnel test seam exists; regression coverage remains a CI/manual gap. |
| `ec19bfb55d9fb8f4c9f35f3637c8b3c1ca33152c` | 2026-10-18T21:49:36Z | Upstream 3.0.0 release metadata | Not imported as-is. The fork remains 2.61.0/0.9.70 and owns its Debian/RPM/changelog history; changing the advertised version is a separate release-identity decision. |
| `452da43bbee0bbf0cb14489bdacce7a08c1936e9` | 2026-10-19T01:21:14Z | Reject RelayRequest from a non-established SSU2 session | Already present and stronger: `HandleRelayRequest` requires `IsEstablished`, plus the fork's state whitelist and bounded validation. |
| `54068be9ca73bfb677617edc330b01eb8f983f37` | 2026-10-19T01:38:25Z | Reject RelayIntro, RelayResponse and PeerTest from a non-established session | Already present and stronger. RelayResponse deliberately permits the `Introduced` hole-punch state; PeerTest additionally requires a remote identity. |
| `472ef88b0eb3fbc3eba5fe8e0b9336a9b8df4499` | 2026-10-19T01:56:37Z | Reject PathChallenge from a non-established session | Adapted: explicit dispatch guard and state helper. `SendPathResponse()` keeps its independent established/address guard. |

The source changes are implemented by static analysis only and are pending
manual CI compilation and tests. No upstream file was copied wholesale and no
merge or cherry-pick was used. `docs/files-changed.md` records the affected
local files; `TODO.md` tracks the remaining NetDb, I2PTunnel and torrent
persistence validation.

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
