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
| Router source | https://github.com/PurpleI2P/i2pd | `openssl` | reviewed through `de32c8c3f580c2c43ca09154ecfe6a20db80d19f` | existing main tree; selective adaptation, no bulk import |

## Reviewed router-source commits (2026-10-18/19/20 UTC)

The following commits were reviewed from `PurpleI2P/i2pd` branch `openssl`.
The window is expressed in UTC and grouped by the author timestamp; the
committer timestamp is used only when it differs, as noted below. Upstream
authors use `-04:00`, `+02:00` and `+03:00`, so a commit's local calendar date
is never a grouping key: the three SSU2 commits dated 18 October `-04:00` are
19 October UTC, while several 20 October `+02:00`/`+03:00` commits are
19 October UTC. `de32c8c3` was the observed branch tip at the end of this
review; 20 October was still in progress, so later upstream commits are not
covered by this pin. This is a review record, not a merge base or a claim that
the fork is at upstream 3.0.0.

Do not enumerate the window with `git log --since/--until` alone: it filters
on the committer timestamp in the local timezone and reproduces the off-by-one
grouping. Use `%H %aI %cI %s`, convert both timestamps with `date -u`, and
account for merge commits separately. The table retains eight earlier
18 October rows for context; **15 commits** fall in the 19–20 October UTC
window.

| Upstream commit | UTC date | Subject | Local result |
|---|---|---|---|
| `18340106ec7623544e0811192c2acd8439d5d626` | 2026-10-18T01:19:38Z | Use `stringstream::view` and check `getline` EOF | Already present through local `9c54986d` in the request-side HTTP tunnel paths. |
| `475f40f8d925afc7519ffdfa59144a62b2a39017` | 2026-10-18T01:55:50Z | Use `view` in the server tunnel and count headers for the connection | Already present through local `9c54986d`; the fork keeps the member header counter and 8192-byte limit. |
| `7dfc05c098cc62a4e3144d2bb54cf985c14c2c68` | 2026-10-18T13:38:10Z | Do not save a piece of a removed torrent | Adapted: weak capture plus authoritative session-membership checks. The 2026-10-21 local follow-up adds irreversible instance tombstones, serialized cleanup and a hash reservation before re-add; regressions are prepared, pending manual CI. |
| `77ebf7516ea44cfe0960831c9161d20983f64130` | 2026-10-18T14:40:57Z | Do not save piece blocks for a stopped torrent | Applied through `Torrent::CanAcceptPieceBlock`, preserving the fork's exact offset/length checks; the piece-level mutex remains in `Piece::BlockReceived`. |
| `9c3a742dc0ae8e5aa4c104683f604185fa3639cb` | 2026-10-18T14:46:15Z | Allow connections from the Debian 12 i2pd package | Applied: the general peer floor is 0.9.57; `NETDB_MIN_HIGHBANDWIDTH_VERSION` remains 0.9.62. The policy is test-pinned; its rationale is recorded in this sync record. |
| `942d6907d2b2cfa73dceaf117457329d251f4191` | 2026-10-18T14:49:01Z | Display the torrents table after the first added torrent | Not applicable: upstream changes `contrib/torrents/web/main.js`; this fork deliberately removed the browser console. The native TUI already keeps the table visible and refreshes after commands. |
| `e3856a38fa0fe310726a39faa9df16f035c1bf69` | 2026-10-18T15:01:02Z | Use `stringstream::view` in the server response and IRC paths | Applied the response/IRC lifetime conversions; the incomplete-header compaction is covered by the `475f40f8` row. The 2026-10-21 local follow-up extracts the production HTTP header parser and prepares `test-i2ptunnel-http` plus fuzz coverage; socket/IRC validation remains pending. |
| `ec19bfb55d9fb8f4c9f35f3637c8b3c1ca33152c` | 2026-10-18T21:49:36Z | Upstream 3.0.0 release metadata | Not imported as-is. The fork remains 2.61.0/0.9.70 and owns its Debian/RPM/changelog history; changing the advertised version is a separate release-identity decision. |
| `452da43bbee0bbf0cb14489bdacce7a08c1936e9` | 2026-10-19T01:21:14Z | Reject RelayRequest from a non-established SSU2 session | Already present and stronger: `HandleRelayRequest` requires `IsEstablished`, plus the fork's state whitelist and bounded validation. |
| `54068be9ca73bfb677617edc330b01eb8f983f37` | 2026-10-19T01:38:25Z | Reject RelayIntro, RelayResponse and PeerTest from a non-established session | Already present and stronger. RelayResponse deliberately permits the `Introduced` hole-punch state; PeerTest additionally requires a remote identity. |
| `472ef88b0eb3fbc3eba5fe8e0b9336a9b8df4499` | 2026-10-19T01:56:37Z | Reject PathChallenge from a non-established session | Adapted: explicit dispatch guard and state helper. `SendPathResponse()` keeps its independent established/address guard. |
| `37090fe09f7effbf65af371e71d3f4df0393514f` | 2026-10-19T11:51:09Z | Add Intel AMT ports to the reserved-port list | Applied: 16992-16995 added to `IsPortInReservedRange`; `test-routercontext-port-range` pins the complete table and the Intel AMT boundaries. |
| `38cf542b13c5d0a39ec42a38522fe08835f6cb71` | 2026-10-19T21:55:52Z | Lower the Debian Boost floor to 1.81 | Not applied as-is: the fork deliberately retains Boost.ProgramOptions 1.74 and already requires Boost.JSON/URL 1.81. Raising the program-options floor adds no needed API and would narrow supported Debian builds; GCC 13 remains the real Debian floor. |
| `f8a12d9dc757a4d225922d8c1b5ec63732ccbf4e` | 2026-10-19T22:14:46Z | Re-add pinned Boost 1.81 alternatives for bookworm | Not applicable: Debian bookworm ships GCC 12, below the fork's GCC 13/C++23 baseline. Alternative Boost package names do not solve the compiler floor. |
| `357fcc14d5b273fbaa67ef8e468a32a8d39ec7e4` | 2026-10-19T22:37:43Z | Add Ubuntu jammy packaging and refresh the UPnP patch | Omitted: `contrib/debian/` and `debian/patches/` were deliberately removed; the patch targets the old GNU make/`filelist.mk` build. Jammy's GCC 11 also cannot build this C++23 tree. The maintained Debian packaging is CMake/Ninja based. |
| `db761a5455e6ca89c1fcc6d135ff634cf17c5dd8` | 2026-10-19T23:31:36Z | Add a CMake torrents switch and RPM dependency gating | CMake half already present and stronger: `WITH_TORRENTS=ON`, target-scoped `NO_TORRENTS`, and Boost.JSON/URL gated on the option. RPM half adapted for Fedora `boost-json`/`boost-url`. The RHEL 9 `-DWITH_TORRENTS=OFF` branch is omitted: its default GCC 11 cannot build this C++23 tree and no validated `gcc-toolset-13` RPM path exists. |
| `a8915cd1d682d168d1a285a465fb2d8296176497` | 2026-10-19T23:56:57Z | `[rpm] fix typo` (missing line continuation) | Not applicable: the fork has no RHEL torrent block after the preceding omission; both specs retain valid `cmake --build` invocations and `build-system-static` rejects direct make/ninja macros. |
| `21ad7d43c81021877a1c01f1bb3e6dc0a11ab708` | 2026-10-20T01:35:49Z | Fix reseed defaults in the sample config | Partially adapted: the fork rejects the `reseed.verify=false` wording because it is fail-closed (`reseed.verify=true`, tested). Applied the built-in reseed-list wording and current URL example in `contrib/i2pd.conf` and `contrib/openbsd/i2pd.conf`; Android has no stale URL example. |
| `e34db4f7cbb3a2732d8742fe5874e8e7ff059e22` | 2026-10-20T01:53:51Z | Merge PR #2604 | Merge only; no tree change (`git diff e34db4f7 21ad7d43` is empty). Disposition is carried by the `21ad7d43` row. |
| `2598a94e4b64cf57eebb33987e6025beefe05b5e` | 2026-10-20T08:44:14Z (committer 08:44:37Z) | Require Boost.URL only on Fedora | Adapted in form: the fork declares Boost.URL only under a defined Fedora guard through F43. The 23-second author/committer skew is retained here; both timestamps are on the same UTC day. |
| `86dd211a08a12b982abab422925dd407f27e7936` | 2026-10-20T10:03:28Z | Do not install Boost.JSON/URL on Mageia | Covered by the Fedora-only Boost condition: Mageia does not install those separate packages. Mageia 9 is below the fork's GCC 13 floor regardless. |
| `233e5111ad5b9b0d4f8636fef6909435ab66dda5` | 2026-10-20T10:10:24Z | Use Ninja on Mageia 9+ | Omitted: the fork uses `cmake --build . --parallel`; it never invokes `make` or `ninja` directly. `%cmake` owns generator selection, and `build-system-static` rejects `_smp_mflags`, `%make_build`, `%ninja_build` and `%ninja` in both specs. |
| `de32c8c3f580c2c43ca09154ecfe6a20db80d19f` | 2026-10-20T10:18:46Z | Use Ninja on Mageia 9 and 11+ | Omitted for the same reason as `233e5111`; this is the review high-water mark. |

The source changes are implemented by static analysis only and are pending
manual CI compilation and tests. The continuation adds the Intel AMT reserved
port table and its regression, adapts the reseed sample wording without
changing the fail-closed verification default, and carries the applicable RPM
Fedora Boost guard. No upstream file was copied wholesale and no merge or
cherry-pick was used. `docs/files-changed.md` records the affected local files;
`TODO.md` tracks the remaining NetDb, I2PTunnel, torrent persistence and
packaging validation.

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

# Router source: fetch into a read-only tracking ref without touching the
# worktree, then verify the pin used by this document.
git fetch --no-tags https://github.com/PurpleI2P/i2pd.git \
  openssl:refs/remotes/upstream/openssl
git rev-parse upstream/openssl

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
