<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: FAQ.

Documents project documentation.

-->

## What is I2P?

I2P (Invisible Internet Protocol) is a universal anonymous network layer. 
All communications over I2P are anonymous and end-to-end encrypted, participants
don't reveal their real IP addresses. 

I2P client is a software used for building and using anonymous I2P 
networks. Such networks are commonly used for anonymous peer-to-peer 
applications (filesharing, cryptocurrencies) and anonymous client-server 
applications (websites, instant messengers, chat-servers).

I2P allows people from all around the world to communicate and share information
without restrictions.

A deeper technical introduction of I2P can be found [here](https://geti2p.net/en/docs/how/tech-intro).

## How openi2pd differs from the original I2P implementation?

While [Java I2P](https://geti2p.net) and openi2pd are both clients for the I2P network, openi2pd has some major differences and advantages:

* Java I2P has built-in applications for torrents, e-mail and so on. openi2pd includes a native I2PSnark-compatible torrent engine and TUI, and other software can connect through I2CP, SAM, BOB, HTTP or SOCKS.
* openi2pd does not require Java. It is written in C++.
* openi2pd consumes less memory and CPU.
* openi2pd targets the supported Linux, OpenBSD and Windows build matrix with a C++23 compiler and CMake 3.24 or newer; see the build guides for current requirements.
* openi2pd has optimizations for faster cryptography, reducing processor time and energy use.

## Why is the I2P network so slow and unstable sometimes?

By design, in the I2P network, your connection gets encrypted through a chain of 6 
random computers in the way to its final destination. If one of those computers
is shut down, or experiences connectivity problems, it can take some time for your
I2P router to discover and fix that issue. 

## Help! openi2pd is not working. What do I do?

First of all, synchronize system clock on your machine with Internet.

If that does not help, on Linux machines, check the number of open file descriptors
allowed to a process. In a terminal, run:

    ulimit -n

A regular router usually needs 4096; a floodfill usually needs 8192.
Set it before starting the router:

    ulimit -n 4096 && ./i2pd

If that does not help, you may have found a bug. Report it through the
[openi2pd issue tracker](https://github.com/daviduhden/openi2pd/issues).

## What is good tunnel creation success rate value?

\>10%. Larger is better.

## Is there a place I can use to find running I2P websites?

Sure, there is a list [here](http://identiguy.i2p.xyz/)

## How can I better integrate my router to the network?

Edit your settings: set correct bandwidth and share rate. 

Run openi2pd for a long time, download and seed some popular torrents.

## What browser should I use to browse I2P websites?

Use any open source browser - for example, Firefox or a Chromium based alternative. Create separate profile for I2P ([firefox instructions](https://support.mozilla.org/en-US/kb/profile-manager-create-and-remove-firefox-profiles)), 
and try not to mix clearnet browsing with I2P. Learn how to configure your browser for better privacy and security.

Configuring [privoxy](https://wiki.archlinux.org/index.php/Privoxy) for I2P/onion/clearnet browsing at the same time is a good idea.

The openi2pd SOCKS proxy can pass non-I2P traffic to a Tor SOCKS proxy. Make sure you understand the privacy implications before enabling this.

## What is a floodfill mode?

Floodfill mode make your router a floodfill, that means what other routers will publish and get LeaseSets and RIs on your router. [Read more](https://i2p.net/en/docs/overview/network-database/).
Floodfill mode require more bandwith, CPU time. You can enable floodfill only if it has public ipv4 address and minimal bandwidth O: `floodfill = true`

## How is I2P different from Tor?

I2P and Tor have some similarities, but they are completely different technologies.

Tor is designed to act as an anonymous proxy for the regular Internet, and I2P 
is specifically designed to make a virtual anonymous network for hidden services 
and p2p applications. 

Tor Project was started by the US military and receives most of its funds
from the government, while I2P was started by a community of independent civilians.

Tor is highly centralized by design, while I2P is designed to be decentralized and distributed.

## Can I use openi2pd as a proxy for the regular Internet?

Yes, but better use [Tor](https://www.torproject.org/) for that.

