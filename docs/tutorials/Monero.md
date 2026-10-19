Connect a Monero wallet to a remote Monero node over I2P
========================================================

> Back up your wallet keys before changing wallet or node settings. The
> examples use the Monero GUI wallet; other wallets may expose equivalent
> remote-node or proxy settings.

This guide configures a Monero wallet to reach a remote Monero node through an
openi2pd client tunnel. The same procedure works with a self-hosted node when
the destination address and port are adjusted.

## Create the client tunnel

Add a client tunnel to `tunnels.conf`:

```ini
[monero]
type = client
destination = <b32 address of the Monero node>
destinationport = 18089
address = 127.0.0.1
port = 18089
keys = monero.dat
```

`destinationport` is the RPC port advertised by the remote node. `port` is the
local loopback port used by the wallet; choose another free port if 18089 is
already in use. Keeping `address = 127.0.0.1` limits access to the local
machine. If the wallet runs on another host, bind only to the required
interface and restrict access with the host firewall.

Restart openi2pd, or reload its tunnel configuration where the service manager
supports it:

```sh
sudo systemctl restart i2pd
```

The `i2pd` service name is retained for compatibility with existing
installations.

## Configure the wallet

1. Open Monero GUI and switch to Advanced mode.
2. Open **Settings → Node**.
3. Select **Remote node** and add `127.0.0.1:18089` (or the local address and
   port selected above).
4. Leave the username and password blank unless the node requires
   authentication.
5. Connect and let the wallet synchronise with the node.

The wallet now reaches the node through the I2P client tunnel. Choose a node
operator you trust; this guide does not endorse a specific provider.
