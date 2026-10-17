Installing Open I2Pd
==========

## Building from source

See [developer section documentation](../devs/building/requirements.md) for how to build i2pd from source on your OS.


## Installation from prebuilt packages

The easiest way to install Open I2Pd is by using precompiled packages and binaries.
Use packages from your operating-system distribution, or build from source. Release artifacts, when published, are on the [Open I2Pd releases page](https://github.com/daviduhden/open-i2pd/releases).


## Windows

Build with the instructions in [Windows](../devs/building/windows.md), or use artifacts from the fork release page. Two architectures are supported: x86 (32-bit) and x86_64. The Windows XP build path was removed because the C++23 toolchain and current dependencies do not support it.


## Android

Build the integrated Android app from `android/` with OpenJDK 25, the Android
SDK and NDK r27+. See [Android app](devs/building/android.md). The upstream
F-Droid package has a different application ID and is not an update path for
this fork.

## Docker images

Build a local image from `contrib/docker/Dockerfile`; no prebuilt upstream image
is required. The container configuration disables the TUI socket because there
is no terminal, and the web console has been removed.

## Linux

### Distro-agnostic GUI

You can install i2pd GUI from Flathub using `flatpak`:

    sudo apt install flatpak
    flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
    flatpak install flathub website.i2pd.i2pd

### Arch

i2pd packages are available at Arch's [extra repository](https://archlinux.org/packages/extra/x86_64/i2pd/) for release version, and [AUR](https://aur.archlinux.org/packages/i2pd-git/) for nightly builds.

You can download them by running 

     pacman -S i2pd --noconfirm

### Gentoo

i2pd [has a working ebuild in the main gentoo repository](https://packages.gentoo.org/packages/net-vpn/i2pd). As of May 2018, the ebuild 
is still listed as "unstable", and thus will request an exception in your package.keywords if you are using gentoo under the "stable" branch.
To install i2pd, enter the command:
    
    emerge --ask net-vpn/i2pd

If you use `gcc` to compile packages and would like to enable cmake's hardening features, use the `i2p-hardening` flag (recommended).

### Debian

Use the `i2pd` package from your Debian/Ubuntu distribution, or build the fork with the maintained `debian/rules` under `debian/`.

Install apt-transport-https package

    sudo apt-get install apt-transport-https

Automatically add repository

    wget -q -O - https://repo.i2pd.xyz/.help/add_repo | sudo bash -s -

After that you can install i2pd as any other software package:

    sudo apt-get update
    sudo apt-get install i2pd

Look for more information about Debian repository [here](https://repo.i2pd.xyz/.help/readme.html).

### Fedora/CentOS

Use the `i2pd` package from your distribution if available, or build from
source with CMake/Ninja. The old third-party COPR repository installed
upstream i2pd, not this fork, and is intentionally not referenced.

### Ubuntu

Use the distribution package or build from source. The old PPA installed
upstream i2pd, not this fork, and is intentionally not referenced.

### Void

You can install i2pd from the /current [xpbs repository](https://github.com/void-linux/void-packages/tree/master/srcpkgs/i2pd)

To install and set the daemon up, run this

    sudo xbps-install -Sy i2pd
    sudo ln -s /etc/sv/i2pd /var/service/

## FreeBSD

You can install i2pd from [ports](https://www.freshports.org/security/i2pd/).

## OpenBSD

You can install i2pd from [ports](https://openports.pl/path/net/i2pd).

## MacOS X

You can install i2pd from [brew](https://brew.sh/) package manager:

    brew install i2pd

or use a static artifact from the fork release page when one is published.

