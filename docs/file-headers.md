<!--
See the LICENSE file at the top of the project tree for copyright
and license details.

openi2pd file header coverage.

Documents formats whose content cannot safely contain comment headers.

-->

<!-- Copyright (c) 2026, David Uhden Collado -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
# File header coverage

Project source, configuration, script and resource files carry a short
reference to the repository `LICENSE` and a file-specific description in their
native comment syntax. The duplicated project copyright/SPDX boilerplate has
been removed; the legal terms remain in `LICENSE` and in the notices of
imported components. Shebangs, XML declarations and Markdown front matter
retain their required positions. This change does not relicense imported
components.

The files below retain their own contents because binary formats, certificates,
protocol fixtures, machine-readable data and legal texts require a different
placement. This inventory provides their project license reference without
inserting data that could alter their meaning. Original file-specific legal
notices and terms take precedence; the repository LICENSE is not a replacement
for third-party licenses. The supplied logo-original.png is unchanged.

| File | Reason for external header |
|---|---|
| `LICENSE` | Legal/provenance text; retained verbatim with its own license terms. |
| `Win32/openi2pd.ico` | Binary asset; an inline text header would corrupt its format. |
| `android/LICENSE` | Legal/provenance text; retained verbatim with its own license terms. |
| `android/app/src/main/assets/addressbook/addresses.csv` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `android/app/src/main/assets/certificates` | Symbolic link; target provenance is retained. |
| `android/fastlane/metadata/android/en-US/full_description.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `android/fastlane/metadata/android/en-US/short_description.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `android/fastlane/metadata/android/en-US/title.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `android/gradle/wrapper/gradle-wrapper.jar` | Binary asset; an inline text header would corrupt its format. |
| `assets/branding/logo-original.png` | Binary asset; an inline text header would corrupt its format. |
| `assets/branding/logo.png` | Binary asset; an inline text header would corrupt its format. |
| `assets/branding/symbol.png` | Binary asset; an inline text header would corrupt its format. |
| `contrib/artwork/openi2pd-256.png` | Binary asset; an inline text header would corrupt its format. |
| `contrib/certificates/family/gostcoin.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/family/i2p-dev.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/family/i2pd-dev.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/family/mca2-i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/family/stormycloud.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/family/volatile.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/acetone_at_mail.i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/admin_at_likogan.dev.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/admin_at_stormycloud.org.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/creativecowpat_at_mail.i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/damian.buho_at_proton.me.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/diyarciftci_at_protonmail.com.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/echelon3_at_mail.i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/freedompeer_at_skhron.eu.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/i2p-reseed_at_mk16.de.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/idk_at_stormycloud.org.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/igor_at_novg.net.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/lazygravy_at_mail.i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/orignal_at_mail.i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/r4sas-reseed_at_mail.i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/rambler_at_mail.i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/reseed_at_diva.exchange.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/reseedserver_at_mail.i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/sahil_at_mail.i2p.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/certificates/reseed/vserod1488_at_proton.me.crt` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/ci/android/vcpkg.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `contrib/static/debian/copyright-bundled` | Legal/provenance text; retained verbatim with its own license terms. |
| `contrib/static/debian/source/format` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `contrib/subscriptions.txt` | Machine-consumed data/list; no uniform comment syntax is guaranteed. |
| `contrib/upstream/docs-extra-files.txt` | Machine-consumed data/list; no uniform comment syntax is guaranteed. |
| `contrib/upstream/docs-files.txt` | Machine-consumed data/list; no uniform comment syntax is guaranteed. |
| `contrib/upstream/docs-path-map.txt` | Machine-consumed data/list; no uniform comment syntax is guaranteed. |
| `contrib/upstream/tools-files.txt` | Machine-consumed data/list; no uniform comment syntax is guaranteed. |
| `debian/GPL-2.0.txt` | Legal/provenance text; retained verbatim with its own license terms. |
| `debian/NEWS` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `debian/changelog` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `debian/compat` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `debian/copyright` | Legal/provenance text; retained verbatim with its own license terms. |
| `debian/docs` | Unclassified text/data format; requires a format-specific header rather than an unsafe insertion. |
| `debian/i2pd.1` | Symbolic link; target provenance is retained. |
| `debian/i2pd.logrotate` | Symbolic link; target provenance is retained. |
| `debian/i2pd.service` | Symbolic link; target provenance is retained. |
| `debian/i2pd.tmpfile` | Symbolic link; target provenance is retained. |
| `debian/source/format` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `docs/LICENSE.i2pd-docs-en` | Legal/provenance text; retained verbatim with its own license terms. |
| `docs/assets/branding/logo-original.png` | Binary asset; an inline text header would corrupt its format. |
| `docs/assets/branding/logo.png` | Binary asset; an inline text header would corrupt its format. |
| `docs/assets/branding/symbol.png` | Binary asset; an inline text header would corrupt its format. |
| `docs/img/favicon.ico` | Binary asset; an inline text header would corrupt its format. |
| `docs/img/logo.png` | Binary asset; an inline text header would corrupt its format. |
| `docs/tutorials/media/http-1.png` | Binary asset; an inline text header would corrupt its format. |
| `docs/tutorials/media/xmpp-2.png` | Binary asset; an inline text header would corrupt its format. |
| `docs/tutorials/media/xmpp-3.png` | Binary asset; an inline text header would corrupt its format. |
| `docs/tutorials/media/xmpp-4.png` | Binary asset; an inline text header would corrupt its format. |
| `docs/tutorials/media/xmpp-5.png` | Binary asset; an inline text header would corrupt its format. |
| `tests/interop/templates/tunnel-build-time-windows.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2cp-blindinginfo-bad-blindtype.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2cp-blindinginfo-bad-blindtype.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2cp-hostlookup-truncated.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2cp-hostlookup-truncated.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-dblookup-excluded-overrun.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-dblookup-excluded-overrun.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-dblookup-missing-peer-count.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-dblookup-missing-peer-count.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-dbsr-missing-from.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-dbsr-missing-from.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-header-bad-checksum.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-header-bad-checksum.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-header-size-overrun.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/malformed/i2np-header-size-overrun.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2cp-blindinginfo-hash.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2cp-blindinginfo-hash.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2cp-hostlookup-hash.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2cp-hostlookup-hash.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-any.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-any.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-elgamal-tag.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-elgamal-tag.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-ls-excluded.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-ls-excluded.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-ratchet-pubkey.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-ratchet-pubkey.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-ratchet-tag.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-ratchet-tag.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-tunnel-elgamal.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dblookup-tunnel-elgamal.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dbsr-empty.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dbsr-empty.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dbsr-two.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/i2np-dbsr-two.hex` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-30-only.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-30-only.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `tests/interop/vectors/valid/sam-hello-31-only.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-31-only.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `tests/interop/vectors/valid/sam-hello-32-only.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-32-only.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `tests/interop/vectors/valid/sam-hello-33-only.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-33-only.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `tests/interop/vectors/valid/sam-hello-max31.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-max31.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `tests/interop/vectors/valid/sam-hello-min30-max33.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-min30-max33.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `tests/interop/vectors/valid/sam-hello-min32.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-min32.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `tests/interop/vectors/valid/sam-hello-noversion.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-noversion.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `tests/interop/vectors/valid/sam-hello-single-digit.case.json` | Certificate or structured/exact-byte data; comment headers are not part of its format. |
| `tests/interop/vectors/valid/sam-hello-single-digit.txt` | Exact-format package, store or protocol fixture data; retained without extra records. |
| `tools/LICENSE` | Legal/provenance text; retained verbatim with its own license terms. |
