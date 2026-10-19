Cross-compilation
=================

> **Status:** no supported cross-compilation workflow is maintained for
> openi2pd. Builds are validated only natively in manual CI.

The supported build paths are native CMake builds:

* Linux and OpenBSD: [Unix build](unix.md)
* Windows: [Windows build](windows.md), using MSYS2/Clang64
* Android: [Android build](android.md)

Experimental cross builds may use a CMake toolchain file and the normal
`cmake -S build -B <dir> -G Ninja` flow, but they are outside the supported
matrix and must not be represented as validated. The old GNU Make,
Boost 1.60 and OpenSSL 1.0.2 instructions from upstream i2pd are obsolete and
have been removed; do not reintroduce them.
