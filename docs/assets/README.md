# Documentation asset mirror

`docs/assets/branding/` is a site-build mirror of the canonical
`assets/branding/` tree. Update it with:

```sh
rm -rf docs/assets/branding
mkdir -p docs/assets/branding
cp -a assets/branding/. docs/assets/branding/
```

The canonical copies and the supplied `logo-original.png` remain at
`assets/branding/`; this mirror exists so MkDocs can publish the relative links
without a runtime plugin or Python hook.
