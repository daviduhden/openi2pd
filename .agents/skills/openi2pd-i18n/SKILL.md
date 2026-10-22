---
name: openi2pd-i18n
description: Use when adding or changing UI translations, language selection, native/Android catalogs, locale fallback or translated-resource tests, excluding documentation translation and protocol/internal identifiers.
---
<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: I18N skill.
-->

# openi2pd internationalization

## Activate when

Use this skill for user-visible text in the native TUI, HTTP proxy pages, daemon
messages and Android resources, including locale selection, catalogs, plural
forms, UTF-8 width handling and translation tests. Route protocol/wire changes
to `openi2pd-i2p-interop`, privilege-separated IPC to `openi2pd-privsep`, and
documentation translation to `openi2pd-documentation`.

Read [`../../../AGENTS.md`](../../../AGENTS.md) first. The official UI locales
are English (`en`, source/fallback), Spanish (`es`), Russian (`ru`) and
Simplified Chinese (`zh-Hans`). `zh-Hant` is optional only with verified
translations.

## Inspect first

- Native catalog: `i18n/I18N.h`, `i18n/I18N.cpp`, `i18n/I18N_langs.h`,
  `i18n/English.cpp`, `Spanish.cpp`, `Russian.cpp`, `ChineseSimplified.cpp`.
- TUI boundary: `tui/main.cpp` (`SetLanguage`, `PutLine`, `DrawHeader`,
  `DrawStatus`, `TerminalUnix.cpp`), `tui/TuiClient.*`,
  `tui/TuiClientWin32.cpp`.
- Android: `res/values/strings.xml`, `values-es`, `values-ru`,
  `values-zh-rCN`, `res/xml/locales_config.xml`, `build.gradle` locale filters,
  `SettingsFragment.java` language preference.
- Tests and checks: `tests/test-i18n-format.cpp`,
  `tests/test-i18n-locale.cpp`, `tests/check-i18n.cmake`,
  `android/tools/check-android-locales.pl`.

## Reference sources

- [`references/locale-matrix.md`](references/locale-matrix.md) — locale tags,
  resource directories, plural categories and source files.
- [`references/identifier-boundary.md`](references/identifier-boundary.md) —
  strings that must never be translated.
- [`../../../docs/android-java25.md`](../../../docs/android-java25.md) — Java 25
  source/runtime and per-app locale constraints.
- [`../../../docs/cpp23-migration.md`](../../../docs/cpp23-migration.md) — native
  language/catalog and UTF-8 decisions.
- [`../../../docs/testing.md`](../../../docs/testing.md) — CTest labels and
  manual validation matrix.

## Reconnaissance

Launch at least one read-only `explore` subagent before editing, split across
independent areas when useful. If subagents are unavailable, report that
limitation; do not substitute local execution. Ask for the exact visible
strings, their call sites, placeholders, plural needs, layout width, and whether
the text crosses a protocol/IPC boundary.

## Parallel implementation

- Keep native catalogs (`i18n/*.cpp`) and Android resources in separate
  workstreams; they have different storage and build paths.
- The coordinator owns `AGENTS.md`, `.agents/skills/README.md`,
  `tests/check-agent-skills.cmake` and shared CMake changes.
- Do not translate protocol fields, IPC verbs, configuration keys, log/JNI
  tokens or persisted values. Translate presentation text at the UI boundary.
- A new official language requires all four native catalogs, all Android
  catalogs, locale metadata, the static check and the manual test lane.

## Conventions

- English source strings are the native catalog keys; translations must preserve
  every printf conversion in count and type. Use positional Android tokens
  (`%1$s`, `%2$d`) and never add a bare `%s`.
- Russian has one/few/many plural forms; Spanish two; Simplified Chinese one.
  Do not fake a missing plural with concatenated counts.
- UTF-8 without BOM, LF endings and natural target-language terminology.
  Product/protocol identifiers and file names stay verbatim.
- Android uses `values/` (English source), `values-es`, `values-ru`,
  `values-zh-rCN`; `locales_config` and `localeFilters` use `en, es, ru, zh`
  with `zh-CN` as the per-app tag. `values-zh` alone is forbidden because it
  would also match Traditional Chinese.
- TUI `Put` takes terminal columns, not bytes; use `DisplayWidth`/`FitUtf8`
  and never advance tab layout by `std::string::size()` for translated text.
- Native selection precedence: explicit `--lang` / configuration, then
  `LC_ALL`/`LC_MESSAGES`/`LANG`, then English. Unsupported and Traditional
  Chinese fall back to English, not Simplified text.
- Android selection uses `AppCompatDelegate.setApplicationLocales` only; never
  `Locale.setDefault` or process-wide `updateConfiguration`. System language is
  represented by an empty locale list.

## Known issues and decisions

- The native catalog is a compile-time `std::map<std::string_view,
  std::string_view>` table; it is installed as code, not runtime `.mo` files.
  Do not add libintl or a runtime locale directory without a packaging and
  sandbox migration.
- `I18N.cpp` is process-local and must not depend on `ClientContext`; the TUI
  links `libi2pdlang` but not the router/client libraries.
- `TerminalUnix::Put` now treats its limit as columns; the Windows backend has
  its own cell-width implementation. Keep both semantics aligned.
- `setlocale(LC_NUMERIC, "C")` is mandatory after UI locale selection so
  progress/ratio formatting stays stable. Do not use `Locale.ENGLISH` for
  Android user-visible numbers; use the active per-app locale.
- Android locale filters and `locales_config` must stay synchronized with the
  resource directories; the compile-free guard checks key/placeholder parity.
- `I18N-003` tracks remaining full TUI help/tool/composed-string coverage;
  the current native core/status/prompt catalogs are real but not a claim that
  every historical tool diagnostic has a translation.
- No compiler, CTest, Gradle, device or manual workflow has validated the
  current translations.

## Do not

- Do not translate protocol keys, IPC commands, configuration keys, log/JNI
  status tokens, bencode fields, paths, user names or torrent names.
- Do not add empty catalogs, machine-translated placeholders or Simplified
  Chinese text in a Traditional Chinese catalog.
- Do not add `Locale.setDefault`, process-wide configuration updates or a
  second locale override path.
- Do not use byte counts for TUI columns or split a UTF-8 sequence.
- Do not create commits, tags, releases, worktrees or temporary commits, and do
  not push; inspect `git status`/`git diff` before editing.
- Do not install dependencies or fetch/vendor code locally to enable a build or
  test. Preserve LICENSE/NOTICE/provenance and technical identifiers/paths
  unless an approved migration exists (AGENTS.md).

## Prepare for CI

- Register a new C++ test in `tests/CMakeLists.txt`; add static catalog checks
  to `tests/check-i18n.cmake` and the compile-free labels.
- Run only source/XML/UTF-8 inspection locally. Do not run Gradle, CTest,
  xmllint or the Android guard locally; the manual workflow runs them.
- Keep the Android guard path in `tools/CMakeLists.txt`, `tools/tests/`,
  `tests/CMakeLists.txt` and `.github/workflows/build.yml` synchronized.
- Record Android translation coverage as prepared until AAPT2/Lint/device
  evidence exists.

## Tracking

Use `I18N-*`/`ANDROID-*` rows and the exact status vocabulary in `TODO.md`.
Record the source catalog, translated catalogs, checker result and pending
manual lanes. Propose a dated `PROGRESS.md` entry and any tracking changes to
the coordinator; append only when explicitly assigned. Record the exact
AGENTS.md `Tracking states` value and state which validations were not run.

## References

- [Locale matrix](references/locale-matrix.md)
- [Identifier boundary](references/identifier-boundary.md)
- [Android Java 25](../../../docs/android-java25.md)
- [C++23 migration](../../../docs/cpp23-migration.md)
- [Testing matrix](../../../docs/testing.md)
