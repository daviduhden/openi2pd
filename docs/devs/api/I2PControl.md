I2PControl interface
=============

I2PControl interface is documented [here](https://geti2p.net/en/docs/api/i2pcontrol)

Requests specific to i2pd
-------------------------

### LocalDestinationInfo

Returns the leasesets a local destination knows about and the streams it has
open, so a service running several tunnels can tell how loaded each one is.

Request parameters:

| name        | description                                        |
|-------------|----------------------------------------------------|
| destination | address of a local destination, b32 or full base64 |

Addresses of the destinations created by tunnels are listed by
`ClientServicesInfo` with the `I2PTunnel` parameter.

Reply:

| name        | description                                                    |
|-------------|----------------------------------------------------------------|
| destination | address of the destination the reply is about                  |
| leasesets   | known leasesets: `address`, `type` (store type), `encType`     |
| streams     | open streams: `id`, `destination`, `sent`, `received` in bytes |

Both lists are arrays and stay arrays when there is nothing in them. If the
address is missing, malformed or does not belong to a local destination,
`error` is returned instead.

Request:

    {"id": 1, "method": "LocalDestinationInfo", "jsonrpc": "2.0",
     "params": {"Token": "1755381293",
                "destination": "ggffhzc7fnl2skhh6ubbxiynei6darp62l4hudb33tl6zvoj5mmq.b32.i2p"}}

Reply:

    {"id": 1, "result": {
       "destination": "ggffhzc7fnl2skhh6ubbxiynei6darp62l4hudb33tl6zvoj5mmq.b32.i2p",
       "leasesets": [{"address": "5xeoyfvtddmo5k3kxzv7b3d5risil6333ntqrr3yvx3yubz5tk3a.b32.i2p",
                      "type": "3", "encType": "6"}],
       "streams": [{"id": "4287608580",
                    "destination": "5xeoyfvtddmo5k3kxzv7b3d5risil6333ntqrr3yvx3yubz5tk3a.b32.i2p",
                    "sent": "611", "received": "519"}]},
     "jsonrpc": "2.0"}
