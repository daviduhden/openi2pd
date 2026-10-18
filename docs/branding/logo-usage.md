# Logo usage

## Variants

- **Full lockup** — symbol above the wordmark. Use when there is enough
  vertical space: README headers, splash/identity pages, release artwork.
- **Horizontal lockup** — symbol to the left of the wordmark. Use in wide
  headers, desktop packaging and documentation headers.
- **Symbol** — use at small sizes, Android launcher/adaptive icons and
  favicons. Do not add extra text inside the symbol.
- **Monochrome** — one-color reproduction for stamps, embroidery, fax or
  constrained print. Preserve the transparent/white node openings.
- **Light** — white/light wordmark and navy replacement for dark backgrounds.
- **Dark** — the normal navy wordmark for light backgrounds.

## Clear space

Keep at least the diameter of a node (about 13% of the symbol height) clear on
all sides. Do not place the lockup against a busy image without a solid
contrast panel.

## Minimum sizes

- Full lockup: 96 px / 24 mm wide.
- Horizontal lockup: 160 px / 40 mm wide.
- Symbol: 16 px / 5 mm; use the standalone symbol below 24 px.
- Wordmark: do not render below 72 px / 18 mm wide.

At very small sizes use `symbol.svg` or `symbol.png`; the three chevrons remain
recognizable while the wordmark would become illegible.

## Prohibitions

Do not stretch, skew, rotate, recolor individual chevrons, remove nodes,
rearrange the layers, add gradients/shadows/outlines, or place the wordmark on
top of the symbol. Do not replace `openi2pd` with `i2pd` or `PurpleI2P` in
user-facing identity. Technical identifiers (`i2pd` binary, protocol names,
existing config paths) remain unchanged for compatibility.

## Backgrounds

- Use `logo-dark.svg` on white/light surfaces.
- Use `logo-light.svg` on navy/dark surfaces.
- The transparent master is intended for surfaces where the surrounding
  background already provides contrast.
- Do not place the blue-only or teal-only elements on a similar hue without a
  neutral container.

## Android and TUI

Android uses the standalone symbol inside the adaptive-icon safe zone; the TUI
uses the text token/color pair, not a bitmap logo. See
[`../android-modernization.md`](../android-modernization.md) and
[`../tui-ipc.md`](../tui-ipc.md).
