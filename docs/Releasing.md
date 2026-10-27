# Releasing

How a release is built, signed and published, and how it reaches the people who already have the app.

## Cutting a release

Releases are fully automated by `.github/workflows/release.yml`. The workflow runs whenever a push to `master` modifies `CHANGELOG.md`. It:

1. Reads the top non-Unreleased `## [x.y.z] - YYYY-MM-DD` heading from `CHANGELOG.md`.
2. Skips if `v<version>` already tagged or if `aimbuddy.versionName` in `gradle.properties` does not match.
3. Builds the release APK on `ubuntu-latest` with JDK 17, Android SDK 35, NDK 29, CMake 3.22.1, and a Gradle cache.
4. Signs the APK with the upload keystore. These repository secrets are **required**, and the job fails rather than publishing an unsigned or debug-signed APK that users could not upgrade:
   - `KEYSTORE_BASE64` (base64-encoded JKS upload keystore)
   - `KEYSTORE_PASSWORD`
   - `KEY_ALIAS`
   - `KEY_PASSWORD`
5. Verifies the APK signature and prints the certificate SHA-256 to the job summary.
6. Creates a GitHub Release tagged `v<version>` with the changelog section as release notes and the APK attached.

To cut a release: bump `aimbuddy.versionName` and `aimbuddy.versionCode` in `gradle.properties`, add a new `## [x.y.z] - YYYY-MM-DD` heading to `CHANGELOG.md`, and push to `master`. The workflow does the rest.

## How the app finds an update

The app reads the project's published releases from the GitHub API and offers the newest one that is newer than itself and has a file named `AimBuddy*.apk`. A pre-release is only offered to someone who already runs a pre-release, so a stable install is never moved onto a beta. Before anything is installed the app checks that the download has the SHA-256 GitHub lists for it, that it is a newer build of the same app, and that it is signed with the same certificate as the installed one. The system's installer checks the signature again. This is why every release has to use the same signing key.

The model store is separate: it reads `model/index.json` from the `master` branch, so models can be added or updated without a release. See [model/README.md](../model/README.md).

## Release signing

Android identifies an installed app by the pair (`applicationId`, signing
certificate). Every published AimBuddy APK must therefore be signed with one
fixed key. If the certificate changes between releases, users cannot upgrade
and the installer reports a package conflict.

Generate the key once and keep it safe (losing it means every user must
uninstall before their next update):

```powershell
keytool -genkeypair -v -keystore upload.jks -alias aimbuddy `
        -keyalg RSA -keysize 4096 -validity 10000
```

**Local release builds.** Create `keystore.properties` in the repo root
(git-ignored):

```properties
storeFile=upload.jks
storePassword=<store password>
keyAlias=aimbuddy
keyPassword=<key password>
```

**CI.** The workflow feeds the same key to Gradle through
`AIMBUDDY_KEYSTORE_FILE`, `AIMBUDDY_KEYSTORE_PASSWORD`, `AIMBUDDY_KEY_ALIAS`,
and `AIMBUDDY_KEY_PASSWORD`, populated from the repository secrets listed below.
Base64-encode the keystore for the `KEYSTORE_BASE64` secret:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("upload.jks")) | Set-Clipboard
```

Each release run prints the signer's SHA-256 fingerprint to the job summary.
Compare it against the previous release. It must be identical.

## Installing a build by hand

```powershell
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

If an install fails with **"App not installed as package conflicts with an
existing package"**, a build of `com.aimbuddy` signed with a *different*
certificate is still registered on the device. Remove it for every user profile
and retry:

```powershell
adb uninstall com.aimbuddy
adb shell pm uninstall --user 0 com.aimbuddy
adb shell pm list packages -u | findstr aimbuddy   REM should print nothing
```

The last command also lists *uninstalled-but-retained* packages, which are the
usual cause when you are sure the app is not installed.
