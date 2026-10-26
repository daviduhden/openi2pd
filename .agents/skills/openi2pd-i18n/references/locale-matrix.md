<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd locale matrix.
-->
# Locale matrix

| UI language | BCP-47 | Native catalog | Android resources | Per-app/config tag | Native forms | Android quantities |
|---|---|---|---|---|---|---|
| English | `en` | `i18n/English.cpp` | `res/values/strings.xml` | `en` | one/other | one/other |
| Spanish | `es` | `i18n/Spanish.cpp` | `res/values-es/strings.xml` | `es` | one/other | one/other |
| Russian | `ru` | `i18n/Russian.cpp` | `res/values-ru/strings.xml` | `ru` | one/few/many | one/few/many/other |
| Simplified Chinese | `zh-Hans` | `i18n/ChineseSimplified.cpp` | `res/values-zh-rCN/strings.xml` | `zh-CN` | one form | one/other (identical text) |

Native plural arity is es 2, ru 3 and zh 1; Android always needs `one` plus
`other` for these catalogs, with Russian adding `few`/`many`. Do not "fix" the
Chinese Android `one` entry by deleting it.

Traditional Chinese (`zh-Hant`/`zh-TW`) is optional. Add `values-zh-rTW` only
with verified Traditional translations; otherwise it must fall back to English,
not to `values-zh-rCN`.

Native selection: `--lang` or `[tui] language`/`lang` first, then
`LC_ALL`/`LC_MESSAGES`/`LANG`, then English. Android selection:
`AppCompatDelegate.setApplicationLocales`, with an empty list meaning system
language. `locales_config.xml` and `localeFilters` must describe the same set.

Static files:

- `i18n/I18N.cpp` — process-local registry and normalization.
- `i18n/I18N_langs.h` — compiled catalog accessors.
- `android/tools/check-android-locales.pl` — key/placeholder/metadata guard.
- `tests/check-i18n.cmake` — native catalog and TUI wiring guard.
