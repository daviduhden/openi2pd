<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: contributing.

Documents project documentation.

-->

Contributing
============

Coding guideline
----------------

1. Dependencies
   - Files from `libi2pd` must not depend on files from `libi2pdclient` or
     `i2pd`.
   - Files from `libi2pdclient` may depend on `libi2pd` but not on `i2pd`.
   - Source lists are explicit in `build/CMakeLists.txt` and
     `tests/CMakeLists.txt`; the old `filelist.mk` is removed.
2. Use C++23. The supported compiler floors are GCC 13, Clang 17 and MSVC
   19.35 (VS 2022 17.5). There is no C++17/C++20 fallback.
3. Do not reinvent a wheel.
   - Prefer an appropriate standard or Boost facility.
   - If a feature exists in both, prefer the standard library.
   - Keep the existing `std::expected` portability shim for OpenBSD libc++
     rather than replacing it with a project-specific error type.
4. Do not add a dependency without discussion. Boost, OpenSSL/LibreSSL and
   zlib are already part of the supported baseline; version floors are in
   [dependencies](../dependencies.md).
5. Follow the surrounding style. There is no project-wide auto-formatter
   requirement, but keep changes reviewable and consistent with adjacent code.
6. When working with binary data, mind endianness. Use the facilities in
   `libi2pd/I2PEndian.h`; do not add compiler-specific byte-swap fallbacks.
7. Use CMake/Ninja or the POSIX make front-end. Do not add GNU make rules,
   submodules, configure-time downloads or a web console.

Documentation and synchronization
---------------------------------

- Keep the fork guides in `docs/` coherent with the real code. The published
  entry point is the [Developer guide](../developer-guide/README.md); the
  upstream import and comparison procedure is
  [upstream-sync.md](../upstream-sync.md).
- Run `perl tools/scripts/check-doc-links.pl docs` and
  `perl tools/scripts/validate-branding.pl` before sending a documentation
  change.
- Do not copy an upstream HTML page into `docs/`: convert it to Markdown,
  preserve headings, anchors, images, tables, code blocks and license text, and
  record the upstream commit in `docs/upstream-sync.md`.
- Do not overwrite fork-owned C++23, IPC, TUI, BitTorrent, Android, CMake, Perl
  or OpenBSD pages with a bulk upstream copy. Merge only corrections that still
  apply to this tree.
- MkDocs is optional publication tooling. The daemon and build must not depend
  on it.
