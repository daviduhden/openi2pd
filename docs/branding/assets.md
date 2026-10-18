# Asset inventory

| File | Type | Notes |
|---|---|---|
| `logo.svg` | SVG, transparent | Full symbol + wordmark, 1254×1254 viewBox |
| `logo.png` | PNG, transparent | Rasterized from `logo.svg` at 1254 px |
| `symbol.svg` | SVG, transparent | Square 1024×1024 standalone mark |
| `symbol.png` | PNG, transparent | Rasterized from `symbol.svg` at 1024 px |
| `logo-horizontal.svg` | SVG, transparent | Symbol + wordmark, 1280×480 viewBox |
| `logo-monochrome.svg` | SVG, transparent | Single navy ink; white node openings |
| `logo-light.svg` | SVG, transparent | For dark backgrounds |
| `logo-dark.svg` | SVG, transparent | Normal lockup for light backgrounds |
| `logo-original.png` | PNG, RGBA | Supplied original reference, unchanged |
| `colors.md` | Markdown | Canonical color tokens and contrast checks |

The SVGs contain only vector paths and metadata. Validation scripts must reject
any `<image>`, `data:image`, or embedded raster payload.

## Regeneration

`logo.png` and `symbol.png` are generated from the SVG masters with
`rsvg-convert` (or an equivalent renderer). Do not hand-edit PNGs. The original
reference is never overwritten.

## Provenance

The original supplied reference is `logo-original.png` (SHA-256
`9e8dc2d7a8e330112f28011ebfe993871a01e9cd8be70c8cb85cb29c0d76112f`).
The SVG masters were reconstructed from that artwork as cubic Bézier paths and
simplified; they do not embed the PNG or any font. The artwork was supplied for
the openi2pd project identity. Copyright/license notices for the project code
remain in `LICENSE`; the supplied artwork is distributed with the project under the same
BSD-3-Clause license as the source unless a separate written license is
provided; it is not represented as an upstream i2pd asset.

## Static validation

```sh
perl tools/scripts/validate-branding.pl
```

This checks required files, rejects embedded raster/font payloads in SVG, and
verifies the master PNG dimensions. It does not replace visual or device
rendering checks.
