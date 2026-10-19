<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: haikuos.

Documents project documentation.

-->

> **Platform note:** this page is retained from upstream i2pd documentation for reference. openi2pd does not build or test this platform; use upstream i2pd for support.

Building on HaikuOS system
=============================

* [HaikuOS](#haikuos)

## HaikuOS
```bash
pkgman refresh
pkgman install boost1.91_devel
# if not installed 
pkgman install make
pkgman install gcc
pkgman install git 
# ^^^ will be
git clone https://github.com/PurpleI2P/i2pd.git
cd i2pd
#make -j#count of your cpu or just make
make
```

after compilation you can run
```bash
./i2pd
```

also click on your time with right mouse button, and open settings, found synchronization and synchronize time its need!
after on Control+C close and your workdir is ~/config/settings/i2pd
You can create there i2pd.conf also tunnels.conf and create tunnels
Also for haiku exists primitive GUI.
