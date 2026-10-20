<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: REBRANDING.

Documents project documentation.

-->

# openi2pd identity and inherited-brand audit

Project identity used for this fork: **openi2pd**. The official geometric
node/tunnel mark and palette are defined in `assets/branding/`; technical
identifiers remain unchanged for compatibility and provenance.

This audit separates brand identity from technical identifiers required for
protocol interoperability, binary compatibility, persisted data and legal
provenance.

## 1. Inventory and classification

| Category | Examples | Decision |
|---|---|---|
| Copyright / license | `Copyright ... The PurpleI2P Project` on upstream/derived files, `LICENSE`, BSD text, docs CC-BY-SA | **Preserved** for derived material; new fork-only files use the fork copyright |
| Upstream provenance | GitHub URLs, i2pd-docs/tools commit pins, upstream manual links | **Preserved** (traceability/sync) |
| I2P protocol identifiers | `i2p`, NTCP2, SSU2, netDb, LeaseSet, router hashes, crypto type names | **Unchanged** (wire compatibility) |
| C++/ABI identifiers | `libi2pd`, `libi2pdclient`, `i2p::...` namespaces, installed headers | **Retained** for ABI/source compatibility |
| Executable | `i2pd` | **Retained temporarily**; rename requires a migration/alias plan (TODO REBRAND-005) |
| Config/data paths | `i2pd.conf`, `~/.i2pd`, `/var/lib/i2pd`, `i2pd.log`, `i2pd-tui.sock` | **Retained temporarily**; migration plan required (TODO REBRAND-006) |
| Android application identity | `org.purplei2p.i2pd`, PurpleI2P package/namespace | **Renamed** to `org.openi2pd.router` |
| Android visible name | `i2pd` | **Renamed** to `openi2pd` |
| Visual identity | i2pd/PurpleI2P app icon, docs logo, favicon, screenshots | **Replaced with the supplied openi2pd mark** |
| Service description | `C++ daemon ...` | **Renamed** to `openi2pd router`; new systemd alias `open-i2pd.service` |
| Marketing/support links | Personal PPA/COPR/F-Droid/donation links | **Removed**; distribution/source links retained |
| Upstream release links | Release artifacts of PurpleI2P | **Replaced** by fork/source guidance where user-facing |

## 2. Applied changes

### Android

- `applicationId` and namespace: `org.purplei2p.i2pd` →
  `org.openi2pd.router`.
- Java package directory and all imports/manifest entries updated.
- JNI header and exported function symbols updated to
  `Java_org_openi2pd_router_I2PD_1JNI_*`.
- App label and visible identity use `openi2pd`; the Fastlane listing is English-only (`en-US`).
- App-specific data directory `i2pd` → `open-i2pd`.
- WebView console, `[http]` preferences, permission gates and broad storage
  permissions removed (previous work).
- Official openi2pd symbol, adaptive icon and splash resources are generated
  from `assets/branding/`; old placeholder and pre-API-29 launcher artwork was
  removed.
- No signing keys or certificates were changed or generated.

**Migration warning:** changing `applicationId` means Android treats this as a
new application. Existing `org.purplei2p.i2pd` installs cannot be updated in
place. Export/backup data first; a future migration build can read the old
app-specific directory and copy it into the new one.

### Desktop/daemon/TUI/tools

- Visible TUI, Windows metadata, `--version` output, Windows error titles, the
  UPnP forwarding display name and the HTTP proxy information page use
  `openi2pd`; the `i2pd` executable and technical identifiers remain unchanged.
- Systemd unit description updated; `Alias=open-i2pd.service` added while the
  historical `i2pd.service` filename remains for package upgrades.
- AppArmor/OpenRC/Upstart/Dinit descriptions updated where they contained
  product branding; paths to the `i2pd` binary remain technical.
- Official symbol/icon resources are generated from `assets/branding/`; Windows
  ICO, desktop icon and documentation favicon/logo replaced.
- Tool executable names (`routerinfo`, `keygen`, etc.) remain; they are CLI
  interfaces, not i2pd branding.
- Visible inherited references removed in this pass include the TUI torrent
  hint, the Android native-log note, the Windows firewall display-name prefix,
  the AppArmor container example, I2PD Browser promotion, personal Monero-node
  guidance and upstream documentation links in generated/config output.
  Documentation and tool links now point to the in-tree guides where they
  exist.
- The obsolete upstream cross-compilation recipe (GNU Make, Boost 1.60,
  OpenSSL 1.0.2) was replaced by a statement of the supported native build
  paths; unsupported platform pages remain explicitly marked as upstream
  references.
- No I2P service endpoints, reseed URLs, certificates or trust anchors were
  changed.

## 3. Retained identifiers and why

| Identifier | Reason |
|---|---|
| `i2pd` executable | Existing services, scripts, packages, documentation and process managers invoke it; renaming without an alias would break upgrades. Pending a compatibility launcher. |
| `libi2pd*` library/header names | Public ABI/API and installed include paths; renaming is an ABI break requiring a major-version transition. |
| `i2pd.conf`, `tunnels.conf`, `~/.i2pd`, `/var/lib/i2pd` | Persisted user data and package layout. Needs dual-read migration and deprecation window. |
| `i2pd.log`, `i2pd-tui.sock`, syslog tag | Operational paths used by logrotate, sandbox `unveil` and monitoring. |
| `i2p::` C++ namespaces | Massive source/API churn with no protocol benefit. |
| I2P protocol names/values | Must remain for wire compatibility. |
| Copyright/license/upstream URLs | Legal and provenance obligations; see `docs/attribution-audit.md`. |

## 4. External services

- Removed: personal PPA/COPR/F-Droid installation instructions, donation/XMR
  address, upstream team list, personal author links.
- Retained: I2P reseed URLs, certificates, address-book subscriptions, I2P
  specification/spec URLs and upstream project provenance. These are
  connectivity/technical or legal references, not fork branding.
- No reseed endpoint was replaced with an invented one.

## 5. Incompatible changes

1. Android application ID change: no in-place upgrade from the upstream
   package. A new installation is required unless a migration build is
   provided.
2. Android package/namespace change: external intents or automation that
   targeted `org.purplei2p.i2pd` components will not resolve.
3. Android app-specific data path changes to `open-i2pd`; old data is not read
   automatically.
4. Systemd alias is additive; existing `i2pd.service` remains valid.

## 6. Pending tasks

- [ ] Decide whether to rename the executable and add an `i2pd` compatibility
  symlink/wrapper (TODO REBRAND-005).
- [ ] Add dual-read migration for `i2pd.conf` → `open-i2pd.conf` and
  `~/.i2pd`/`/var/lib/i2pd` → new paths (TODO REBRAND-006).
- [ ] Rename Debian/RPM source/package names with `Provides/Replaces` and
  changelog migration (TODO REBRAND-007).
- [x] Replace placeholder icon with the supplied openi2pd identity
  (BRAND-001..004; visual validation pending).
- [ ] Scan compiled artifacts/APKs for residual brand strings when builds are
  available.
- [ ] Android migration from old applicationId (TODO REBRAND-009).

## 7. Validation performed

- Recursive text search for `PurpleI2P`, `i2pd`, `I2Pd`, `I2PD` across the tree.
  Remaining occurrences were classified; no global replacement was performed.
- Confirmed no old Android package strings remain under `android/app/src/main`.
- The static branding validator rejects platform hex values absent from
  `assets/branding/colors.md`, verifies the MkDocs colour mirror is identical,
  and still checks SVG/PNG assets. The offline documentation link checker
  reports 100 Markdown files with no broken relative links or local anchors.
- An earlier Android Java 25 revision compiled with Gradle 9.8.1/AGP 9.4.1
  and JDK 25. The latest JNI/lifecycle edits are pending CI compilation;
  APK/JNI/Lint/device validation is deferred to CI.
- CMake C++ build and 20/20 CTest passed at the Session 7 checkpoint; the
  2026-10-18 Java 2.13.0 transport/netDb/LeaseSet recheck reached 48/48 on Linux/GCC 16. Other platform
  validation remains delegated to CI.
- Icon files regenerated and verified as valid PNG/ICO; Android vector XML
  replaced. `validate-branding.pl` now also checks derived documentation,
  packaging, Windows and Android icon dimensions, exact SVG mirrors/copies and
  brand-vector fill colours.
- Dynamic Android/APK and Windows icon checks remain pending.
