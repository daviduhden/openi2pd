<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: colors.

Documents project documentation.

-->

# openi2pd color tokens

This file is the canonical color source for openi2pd. Platform files must
reference these values rather than introducing independent brand hex codes.

## Core logo palette

| Token | Value | Role |
|---|---|---|
| `openi2pd-teal` | `#00B2B4` | Network/tunnel accent, identity mark |
| `openi2pd-blue` | `#078DE4` | Interactive/action accent |
| `openi2pd-navy` | `#162737` | Primary text, dark surfaces |
| `openi2pd-white` | `#FFFFFF` | Node highlights, contrast |

The values above were sampled from the supplied logo. The teal and blue are
graphic accents: they do **not** meet 4.5:1 contrast on white for normal text.
Use the derived semantic tokens below for text and controls.

## Semantic tokens — light theme

| Token | Value | Use |
|---|---|---|
| `primary` | `#007C7A` | Primary actions, selected controls, links |
| `on-primary` | `#FFFFFF` | Text/icons on primary |
| `primary-container` | `#C8F5F2` | Filled selected states |
| `on-primary-container` | `#003735` | Content on primary container |
| `secondary` | `#0067B1` | Secondary actions and focus |
| `on-secondary` | `#FFFFFF` | Text/icons on secondary |
| `secondary-container` | `#D3E9FF` | Secondary filled states |
| `on-secondary-container` | `#002F4F` | Content on secondary container |
| `tertiary` | `#162737` | Strong text, headers, dark surfaces |
| `on-tertiary` | `#FFFFFF` | Content on tertiary |
| `tertiary-container` | `#DCE7EF` | Neutral containers |
| `on-tertiary-container` | `#162737` | Content on neutral containers |
| `on-background` | `#162737` | Text on background |
| `outline-variant` | `#DCE7EF` | Decorative dividers, inactive fills |
| `on-error` | `#FFFFFF` | Content on error |
| `error-container` | `#FFDAD6` | Error containers (Material default) |
| `on-error-container` | `#410002` | Content on error containers |
| `surface-container-lowest` | `#FFFFFF` | Lowest container |
| `surface-container-low` | `#FFFFFF` | Low container |
| `surface-container-high` | `#DCE7EF` | High container |
| `surface-container-highest` | `#DCE7EF` | Highest container |
| `ic-launcher-background` | `#FFFFFF` | Android launcher/splash plate |
| `chart-download` | `#007C7A` | Download chart series |
| `chart-upload` | `#0067B1` | Upload chart series |
| `chart-grid` | `#DCE7EF` | Chart grid lines |
| `background` | `#F7FAFC` | App background |
| `surface` | `#FFFFFF` | Cards, sheets, dialogs |
| `on-surface` | `#162737` | Primary text |
| `surface-variant` | `#DCE7EF` | Dividers, inactive fills |
| `on-surface-variant` | `#3F5263` | Secondary text |
| `outline` | `#5C6E7E` | Borders and outlines |
| `selected` | `#D6F4F1` | Selected rows/tabs |
| `focus` | `#0067B1` | Keyboard/accessibility focus |
| `disabled` | `#A7B5C0` | Disabled content |
| `error` | `#B3261E` | Errors |
| `warning` | `#8A5A00` | Warnings |
| `success` | `#1E6B45` | Success/connected state |

## Semantic tokens — dark theme

| Token | Value | Use |
|---|---|---|
| `primary` | `#59D8D4` | Primary actions, selected controls |
| `on-primary` | `#003735` | Content on primary |
| `primary-container` | `#00504E` | Filled selected states |
| `on-primary-container` | `#C8F5F2` | Content on primary container |
| `secondary` | `#7CC7FF` | Secondary actions and focus |
| `on-secondary` | `#002F4F` | Content on secondary |
| `secondary-container` | `#004A73` | Secondary filled states |
| `on-secondary-container` | `#D3E9FF` | Content on secondary container |
| `tertiary` | `#B7C7D6` | Strong secondary text |
| `on-tertiary` | `#1B2B3A` | Content on tertiary |
| `tertiary-container` | `#273746` | Neutral containers |
| `on-tertiary-container` | `#DCE7EF` | Content on neutral containers |
| `on-background` | `#E4EDF4` | Text on background |
| `outline-variant` | `#273746` | Decorative dividers, inactive fills |
| `on-error` | `#690005` | Content on error |
| `error-container` | `#93000A` | Error containers (Material default) |
| `on-error-container` | `#FFDAD6` | Content on error containers |
| `surface-container-lowest` | `#0B141C` | Lowest container |
| `surface-container-low` | `#101C26` | Low container |
| `surface-container-high` | `#273746` | High container |
| `surface-container-highest` | `#273746` | Highest container |
| `ic-launcher-background` | `#FFFFFF` | Android launcher/splash plate |
| `chart-download` | `#59D8D4` | Download chart series |
| `chart-upload` | `#7CC7FF` | Upload chart series |
| `chart-grid` | `#273746` | Chart grid lines |
| `background` | `#0B141C` | App background |
| `surface` | `#101C26` | Cards, sheets, dialogs |
| `on-surface` | `#E4EDF4` | Primary text |
| `surface-variant` | `#273746` | Dividers, inactive fills |
| `on-surface-variant` | `#C3D1DC` | Secondary text |
| `outline` | `#91A2B1` | Borders and outlines |
| `selected` | `#123B3A` | Selected rows/tabs |
| `focus` | `#7CC7FF` | Keyboard/accessibility focus |
| `disabled` | `#4A5A69` | Disabled content |
| `error` | `#FFB4AB` | Errors |
| `warning` | `#FFD48A` | Warnings |
| `success` | `#7ADFA0` | Success/connected state |

## Contrast checks

The following pairs were calculated with the WCAG relative-luminance formula:

| Pair | Ratio |
|---|---|
| light `on-surface` / `surface` | 15.22:1 |
| light `on-surface-variant` / `surface-variant` | 6.43:1 |
| light `on-primary` / `primary` | 5.04:1 |
| light `on-secondary` / `secondary` | 5.87:1 |
| dark `on-surface` / `surface` | 14.57:1 |
| dark `on-surface-variant` / `surface-variant` | 7.83:1 |
| dark `on-primary` / `primary` | 7.63:1 |
| dark `on-secondary` / `secondary` | 7.55:1 |

Do not use `openi2pd-teal` or `openi2pd-blue` as normal-size text on a light
surface. For small teal/blue text, use the corresponding semantic token.

## Platform mappings

The numeric palette above is the only source of brand values. Platform files
implement the semantic roles and must not introduce independent hex codes:

| Platform | File |
|---|---|
| Android light theme | `android/app/src/main/res/values/colors.xml` |
| Android dark theme | `android/app/src/main/res/values-night/colors.xml` |
| Android launcher/vector marks | `android/app/src/main/res/drawable/ic_logo*.xml`, `ic_launcher_*.xml` |
| ncurses TUI | `tui/Theme.h` |
| Documentation CSS | `docs/stylesheets/brand.css` |
| Documentation header/favicon | `docs/img/logo.png`, `docs/img/favicon.ico` |
| Windows executable/installer icon | `Win32/openi2pd.ico` |
| Desktop/packaging icons | `contrib/artwork/open-i2pd-icon.svg`, `openi2pd-256.png` |
| HTTP proxy error page | `libi2pd_client/HTTPProxy.cpp` |

`tools/scripts/validate-branding.pl` rejects any hex value in those files that
is not listed in this document. The terminal theme approximates these values
for 8-, 16- and 256-colour terminals and provides a monochrome fallback.
