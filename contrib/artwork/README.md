# openi2pd packaging artwork

These files are derived from the canonical masters in `assets/branding/` and
must not be edited by hand.

| File | Source | Purpose |
|---|---|---|
| `open-i2pd-icon.svg` | `assets/branding/symbol.svg` | Scalable application icon |
| `openi2pd-256.png` | `assets/branding/symbol.svg` | 256×256 hicolor icon |

Regenerate with:

```sh
rsvg-convert -w 256 -h 256 assets/branding/symbol.svg \
  -o contrib/artwork/openi2pd-256.png
cp assets/branding/symbol.svg contrib/artwork/open-i2pd-icon.svg
```

The Windows icon `Win32/openi2pd.ico` is generated from the same symbol at
16, 24, 32, 48, 64, 128 and 256 pixels:

```sh
for s in 16 24 32 48 64 128 256; do
  rsvg-convert -w "$s" -h "$s" assets/branding/symbol.svg \
    -o "/tmp/openi2pd-$s.png"
done
magick /tmp/openi2pd-*.png Win32/openi2pd.ico
```

`assets/branding/colors.md` is the color source of truth. The original
supplied reference `assets/branding/logo-original.png` is never modified.
