# Attribution and personal-reference audit

Scope: all 399 tracked files in the working tree (198 C/C++ source and header
files, Markdown, CMake, scripts, packaging and CI), excluding only `.git/`
history and build output. No commit, tag or push was created.

## Rule applied

- **Removed:** personal author lines, obsolete maintainer metadata and
  non-essential individual credits that are not required to build, use or
  understand the software.
- **Preserved:** copyright notices, license texts, SPDX/legal notices,
  third-party module licenses, upstream repository references needed for
  synchronization, and technical references whose removal would break
  protocol behavior or provenance.
- **Not changed:** Git commit authorship, `ChangeLog`, and historical
  packaging changelog entries. Those are provenance records, not current
  maintainer claims. New top-level packaging entries describe the fork.

## References removed or rewritten

| File | Removed/rewritten reference | Reason |
|---|---|---|
| `contrib/apparmor/usr.bin.i2pd` | `# Author: Darknet Villain <...>` | Non-required personal credit in a functional profile |
| `debian/i2pd.init` | `# Author: hagen <...>` | Obsolete init-script credit; no technical function |
| `debian/i2pd.1` | Named `AUTHOR` credits for kytv, hagen and R4SAS | Not a license notice; GPL permission text retained |
| `debian/control` | `Maintainer: r4sas <...>` | Obsolete upstream maintainer metadata |
| `contrib/static/debian/control` | `Maintainer: r4sas <...>` | Same |
| `debian/NEWS` | Personal trailer on an old release note | News entry remains, author replaced by project maintainers |
| `debian/changelog` | New fork entry added; old historical entries retained | Current packaging metadata reflects the fork; old entries are provenance |
| `contrib/rpm/i2pd.spec`, `i2pd-git.spec` | New fork changelog entry; old entries retained | Same |
| `docs/about/community.md` | "PurpleI2P team and community" heading and personal webclient link | Community information remains; no personal credit |
| `docs/index.md` | Upstream release/issue/social links replaced with fork/issues and technical upstream links | Documentation reflects this fork |
| `docs/user-guide/install.md` | Instructions that installed upstream through personal PPA/COPR/F-Droid repositories | Those packages are not this fork; replaced with distribution/source guidance |
| `android/README.md` | Team list, donation/XMR address, upstream release badge and signing fingerprint | Not required to build/use the app; current fork metadata replaces the upstream team list |
| `android/app/src/main/java/.../WebConsoleActivity.java` | Removed with the WebView console | Feature removed by project requirement |

No source file lost its copyright header. The new TUI and protocol sources use
the existing project copyright line (`The PurpleI2P Project`) because they are
derived from the same codebase; no new personal attribution was added.

## Preserved by legal obligation

| Location | Preserved content |
|---|---|
| `LICENSE` | BSD-3-Clause copyright and text |
| Every C/C++ header/source | `Copyright (c) ... The PurpleI2P Project` |
| `debian/copyright` | All upstream and packaging copyright holders, including Kill Your TV, hagen, R4SAS and Yangfl |
| `docs/LICENSE.i2pd-docs-en` | CC-BY-SA 4.0 copyright for imported documentation |
| `docs/about/license.md` | Documentation license text |
| `tools/LICENSE` | BSD license for imported tools (as published upstream) |
| `build/cmake_modules/FindCheck.cmake` | Copyright and license of the CMake module authors |
| `build/cmake_modules/GetGitRevisionDescription.cmake(.in)` | Copyright and license of Ryan Pavlik |
| `Win32/Resource.rc2`, `build/win_installer.iss`, `build/build_mingw.cmd` | `LegalCopyright` / copyright notices |
| `debian/changelog`, `contrib/rpm/*.spec` `%changelog` | Historical attribution and packaging provenance |
| `ChangeLog` | Historical project record |
| `docs/about/community.md` | Community channels, retained as technical/community information |
| Upstream repository URLs in build/provenance docs | Needed for license compliance and future synchronization |

## Preserved for technical reasons

- `docs/devs/building/android.md` still references the external
  `i2pd-android` project because this repository does not contain the Android
  app; the link is a dependency/provenance reference, not a credit.
- `docs/devs/building/cross.md` and the unsupported-platform pages retain
  upstream clone URLs and are explicitly marked as upstream reference material.
- `docs/tutorials/Filesharing/qBittorrent.md` keeps an example I2P tracker
  address; it is protocol/configuration data, not attribution.
- Personal names inside I2P protocol data, certificate common names, test
  vectors or examples were not changed.
- `debian/copyright` names are retained because they are copyright notices for
  files that remain in the package.

## Ambiguous cases

1. **`docs/UPSTREAM-README.md`** is imported upstream project documentation
   with edit links. It is kept as provenance/traceability rather than as
   current maintainer information.
2. **Old Debian/RPM changelog entries** name previous maintainers. They were
   retained as historical records; only a new top entry was added. If the
   project wants a strictly pseudonymous package history, those entries can be
   archived separately, but that would reduce provenance.
3. **`debian/copyright`** includes packaging copyright holders whose files were
   modified. Their notices remain because the files are still derived from
   their work; this is a legal requirement, not an endorsement.
4. **`tools/LICENSE`** has an empty copyright holder field upstream. It was not
   invented or filled in.

## Verification performed

- Searched for `Author`, `Authors`, `Written by`, `Created by`, `Maintained by`,
  `Maintainer`, `Originally`, personal names, and email addresses across code,
  CMake, scripts, docs, packaging and CI.
- Re-read the modified files to ensure only comments/metadata changed.
- Rebuilt the router, TUI and all tools after the cleanup; 16/16 CTest tests
  passed, including `tools-smoke`.
- CI remains manual-only (`workflow_dispatch`); no triggers were restored.

## Validation limits

This audit is a static text and metadata review. It does not determine legal
ownership, and it cannot identify references written without a recognizable
name or email. Copyright/license notices were deliberately left untouched, so
no legal conclusion should be inferred from their presence or absence.
