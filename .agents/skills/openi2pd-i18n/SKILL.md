---
name: openi2pd-i18n
description: Use when adding or changing UI translations, language selection, native/Android catalogs, locale fallback or translated-resource tests, excluding documentation translation and protocol/internal identifiers.
---

## Maintenance & Continuous Learning

When this skill guides a repository change, review it after implementation. Update procedures, tools, configurations, assumptions, examples and validation steps when they changed or when reusable knowledge improves future work; correct inaccurate guidance and omit one-off execution details. Report synchronization in the task summary. Follow the normative process and Definition of Done in [AGENTS.md](../../../AGENTS.md#continuous-agent--skills-maintenance).
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
  `i18n/check-i18n-catalogs.pl` (455-key/14-plural-group authority) and
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

- `tr`/`ntr` are canonical `i2p::i18n` templates with global using aliases for
  historical callers. Keep both forms in `test-i18n-locale`; locale accessor
  declarations in `I18N_langs.h` include `<memory>` directly.
- Every Android `values/strings.xml` entry needs an es/ru/zh-CN translation
  (the guard rejects a missing key). Prefer reusing an existing resource over
  adding a near-duplicate; `address_book_blinded` replaced a hard-coded
  `(blinded)` in a Java row label so the marker is translatable. Titles built
  from a daemon-supplied name need no resource.

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

- New user-facing TUI/Android strings must be added in English *and* to the
  es/ru/zh-Hans catalogs. A missing native key currently falls back to the
  English source (functional but untranslated). The application-service labels
  are translated; the open residual is `I18N-007` (Help bodies and `--help`
  prose) and `I18N-003` (standalone-tool/composed strings). Never present an
  untranslated string as a translation or cite a closed task as the backlog.

- Native catalogs are keyed by the full English source string. C++ joins
  adjacent literals before lookup, so a multi-line `tr(...)` argument is one
  key; a translation stored against only the first fragment never matches.
  `i18n/check-i18n-catalogs.pl` harvests full arguments and verifies parity.
- The catalog checker must concatenate adjacent C++ literals in both catalog
  keys and values as well as harvested call sites; otherwise valid multi-line
  translations appear to be missing.
- `SetStatus`/`BeginPrompt` translate at their boundary. `PutLine` only
  translates an exact full line and never sees the `"> "`/`"! "` status prefix,
  so a catalog entry for a status message is unreachable without the boundary
  lookup. Confirmation hints must match `IsAffirmative` (`y/yes/s/sí/д/да`).
- Plural lookup is keyed by the English plural text and the catalogs provide
  the forms (es two, ru three, zh one). Use `ntr` for native counts and
  `<plurals>`/`getQuantityString` for Android; never concatenate a count with
  an English plural. `ntr("node", "nodes", 5)` in Russian returns `узлов`.
- Layout uses display columns (`DisplayWidth`/`wcwidth`), never byte length.
  The status/NetDb label column is 26 cells; a longer translation is truncated
  deliberately, so abbreviate before widening. Android placeholders are
  positional and `<plurals>` items must keep the same conversions.
- Android user-visible validation errors use resource ids through
  `TorrentControl.UserMessageException`; hardcoded English is a defect. The
  log row, notification channel and torrent labels carry resources. `zh-TW`/
  `zh-Hant` fall back to English and `LocaleResourceTest` pins that.
- Machine-readable CLI output, `--help`, protocol verbs, field names, config
  keys, log tokens, status wire values and hashes stay English. `autoconf_i2pd`
  uses the shared catalogs; other tools resolve the environment lazily.

- Canonical tag mapping: native/config `en`, `es`, `ru`, `zh-Hans`; Android
  resources `en`, `es`, `ru`, `zh-CN` (`values-zh-rCN`). `zh-TW`/`zh-Hant`/`zh-HK`
  fall back to **English**; never present Simplified text as Traditional.
  `docs/i18n.md` is the single policy page and must be linked from README,
  `docs/index.md` and the configuration table.
- Native plurals: es 2, ru 3, zh 1 form. Android requires `one`+`other` for
  every locale (Russian adds `few`/`many`); the Chinese `one` and `other` texts
  are identical. Do not "fix" the Android `one` entry by removing it.
- Documentation is English-only; store metadata is English-only unless a
  reviewed translation is added. UI catalogs and protocol identifiers remain
  separate.


- A single-argument `tr()` returns `std::string_view`; binary `+` with a
  `std::string`/`const char*` operand is ill-formed in C++23. Wrap it in
  `std::string(...)` before concatenation (TUI status/footer builders do this).
  `check-i18n.cmake` rejects the unwrapped pattern in `tui/main.cpp`; a new
  rendering helper must follow the same shape.

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
- Run only compile-free source/XML/UTF-8 inspection locally:
  `perl i18n/check-i18n-catalogs.pl`, `perl android/tools/check-android-locales.pl`,
  `cmake -P tests/check-i18n.cmake` and text/XML parsing are allowed. Do not
  run Gradle, AAPT2, Lint, CTest, a device or the full manual workflow.
- Keep the Android guard path in `tools/CMakeLists.txt`, `tools/tests/`,
  `tests/CMakeLists.txt` and `.github/workflows/build.yml` synchronized.
- Record Android translation coverage as prepared until AAPT2/Lint/device
  evidence exists.

- `i18n/check-i18n-catalogs.pl` parses adjacent C++ literals with a repeated
  capture group. Any nested `=~` match resets `$1`/`$2`, so snapshot the
  literal runs before extracting fragments; otherwise values silently become
  empty and the checker reports false missing translations.
- The Android and native catalogs are keyed by the full concatenated C++
  literal. A multi-line `tr()`/`SetStatus()`/`BeginPrompt()` argument is one
  key, never its first fragment.

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
