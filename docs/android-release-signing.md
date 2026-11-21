# Android release signing

The Android package is `dev.uhden.i2pd`. The Gradle module is `:i2pd`
in `android/app`. Java and JNI retain `org.openi2pd.router`. Android
installs this package alongside the old package IDs, with separate app
data and permissions. Export any data needed from earlier installations.

The production signing certificate SHA-256 fingerprint is
`7d601c2bdf73a438589234156e85aad24d431e27d030488419f6289e2c30332e`.
GitHub Releases had no published assets at provisioning time. The
previous arm64 CI release artifact was unsigned. If a distribution
channel already uses the new package ID, compare its certificate and
version code before offering an update.

## Build and sign

Publish the reviewed workflow on `main`, then dispatch `Android` with
`sign_release=true`. The build uses Java/JDK 25, Gradle 9.8.1, AGP
9.4.1, SDK 37, Build Tools 36.0.0, NDK 30.0.16248370 and CMake 3.31.6.
The signed distribution lane selects the existing arm64 APK, including
`libi2pd.so` and `libc++_shared.so`. The x86_64 emulator lane keeps
its existing opt-in and test signing. Gradle keeps the existing per-ABI
version-code calculation.

The separate signer waits for approval in `android-openi2pd-release`.
Review the exact commit and artifact source before approving. The
environment permits the `main` branch and requires approval by the
repository owner, who can approve their own dispatch. The signer
downloads by artifact ID, verifies its SHA256SUMS, checks API 29 and
package identity, aligns
uncompressed native entries to 16 KiB and signs with APK schemes v2
and v3 after checking the keystore certificate. The build separately checks ELF LOAD alignment for the two
JNI libraries. The signer verifies the expected certificate, rejects
debuggable APKs or a debug signer, and checks that signature processing
preserved package metadata and native library bytes.

Download `openi2pd-android-release` from the successful run and verify
`SHA256SUMS` with `sha256sum -c SHA256SUMS`. Attach
`openi2pd-release.apk`, `SHA256SUMS` and `build-provenance.json` to the intended GitHub Release
manually. Confirm the release points to the build commit and that its
version code increases relative to the last published version of this
package. Retain the same signing certificate for updates. Run
`apksigner verify --verbose --print-certs openi2pd-release.apk` and compare
the SHA-256 digest before distribution.

## Key custody and recovery

The local PKCS#12 and public PEM reside in
`~/.local/share/android-signing/openi2pd/`. Keep the directory at mode
0700 and private files at mode 0600. The portable GnuPG backup `openi2pd-release.p12.gpg` decrypts directly
to the PKCS#12, as do the anon and nero backups. A legacy encrypted TAR
backup also remains available. Both copies were checked against the original. Keep an offline copy of the archive
and retain both the GnuPG recovery passphrase and the PKCS#12 password
in separate custody.

To recover, create an empty private directory at mode 0700. Enter the
recovery passphrase through GnuPG pinentry when decrypting. Write the
restored PKCS#12 into that directory at mode 0600.

```sh
umask 077
mkdir -m 700 /path/to/private-restore
gpg --output /path/to/private-restore/openi2pd-release.p12 \
  --decrypt openi2pd-release.p12.gpg
cd /path/to/private-restore
keytool -exportcert -rfc -alias openi2pd -keystore openi2pd-release.p12 \
  -file restored.pem
openssl x509 -in restored.pem -noout -fingerprint -sha256
```

Confirm decryption succeeded before trusting the
restored files. Match the fingerprint above before refreshing GitHub
environment secrets. Pass the Base64 keystore and its password to
`gh secret set --env android-openi2pd-release` through standard input.
Keep the original key identity. PKCS#12 uses the same password for the
store and key entry, so the workflow has two secrets.

## Android Developer Console

Use the verified full-distribution account to register
`dev.uhden.i2pd` in Packages and add the certificate fingerprint above.
If the Console requests ownership proof, use its exact snippet as
`assets/adi-registration.properties` in a verification APK for this
package, signed with the registered production key. For an app-source
build, that is `android/app/src/main/assets/adi-registration.properties`.
Build the verification APK in a separately approved private CI run,
with the challenge supplied only to its build job and the same isolated
signing procedure. Keep the snippet and the resulting verification APK
out of public commits, artifacts and Releases. Submit the signed
verification APK to the Console and confirm the package status.
Google receives the verification APK and public fingerprint. The key
stays in the configured signing custody.

See the [Android Developer Console guide](https://developer.android.com/developer-verification/guides/android-developer-console)
and the [APK alignment reference](https://developer.android.com/tools/zipalign).

The first signed CI run and Console registration are pending.

The release artifact also contains build-provenance.json. Compare its
revision, repository and run identifiers with the selected workflow run.
Compare its APK digest with SHA256SUMS and its certificate digest with the
local apksigner verification. This manifest is emitted by CI and is not
an independent attestation.
