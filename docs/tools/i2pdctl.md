# i2pdctl

Python 3 client for the retained I2PControl HTTPS service.

## Syntax

```sh
i2pdctl [-h] {exec,raw_info,info} ...
```

Environment: `I2PCONTROL_URL` (default `https://127.0.0.1:7650/`) and
`I2PCONTROL_PASSWORD` (default `itoopie`; change it in the router
configuration).

## Examples

```sh
i2pdctl info
i2pdctl raw_info RouterInfo ClientServicesInfo
i2pdctl exec RouterManager '{"Shutdown":""}'
```

## Security

The script disables TLS certificate verification (`ssl._create_unverified_context`)
because i2pd generates a self-signed management certificate. Only use it on a
trusted local link; do not expose I2PControl to an untrusted network. The
default password must be changed before enabling the service.

## Compatibility

Targets the I2PControl JSON-RPC methods implemented by `daemon/I2PControl*`.
It is not part of the C++ build and has no third-party Python dependencies.
