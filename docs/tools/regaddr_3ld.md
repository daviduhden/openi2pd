<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd: regaddr 3ld.

Documents project documentation.

-->

# regaddr_3ld

Create a signed three-level subdomain registration record.

## Syntax

```sh
regaddr_3ld step1 <subdomain-keys> <subdomain> > step1.txt
regaddr_3ld step2 step1.txt <domain-keys> <domain> > step2.txt
regaddr_3ld step3 step2.txt <subdomain-keys> > step3.txt
```

## Example

```sh
regaddr_3ld step1 sub.keys sub.example.i2p > step1.txt
regaddr_3ld step2 step1.txt example.keys example.i2p > step2.txt
regaddr_3ld step3 step2.txt sub.keys > step3.txt
```

## Security

Each step signs with the corresponding private key. Keep step files and keys
private until the final record is submitted.

## Compatibility

Produces the `addsubdomain` record format with `olddest`, `oldname`, `oldsig`,
and final `sig` fields. The final record can be checked with `verifyhost`.
