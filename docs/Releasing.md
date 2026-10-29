# Releasing

How a release is built, signed and published, and how it reaches the people who already have the app.

## Version numbers

AimBuddy is in beta, so every version looks like `0.3.5-beta.1`: `MAJOR.MINOR.PATCH-beta.N`.

- A release that adds or fixes a handful of things raises the patch number: `0.3.5`, then `0.3.6`. The minor number (`0.4.0`) is for a bigger step with new features, and the `-beta` tag goes away only when the project calls itself stable.
- The beta number counts the releases of one patch number and starts again at 1 when the patch number changes: `0.3.5-beta.1`, `0.3.5-beta.2`, then `0.3.6-beta.1`.
- `aimbuddy.versionCode` in `gradle.properties` goes up by one with every release, whatever the name. Android refuses to put a lower code over a higher one, and the app's updater only offers a build with a higher code than the installed one. Version 0.3.5-beta.1 has code 6 and 0.3.5-beta.2 has code 7.
- The name is written in two places that have to match: `aimbuddy.versionName` in `gradle.properties` and the heading `## [0.3.5-beta.1] - YYYY-MM-DD` at the top of the released part of `CHANGELOG.md`. The workflow stops when they differ.

## Cutting a release

1. Raise `aimbuddy.versionName` and `aimbuddy.versionCode` in `gradle.properties`.
2. In `CHANGELOG.md`, rename `## [Unreleased]` to the new heading and put an empty `## [Unreleased]` above it. The section under the new heading becomes the release notes, so it has to read well on its own: a line on what the release is, then Added, Changed, Removed and Fixed, then an upgrade note when something needs one.
3. Commit and push to `master`. A push that changes `CHANGELOG.md` is what starts the workflow. Do not tag by hand: the workflow tags the commit it built when it publishes, and it skips a version whose tag already exists.

`.github/workflows/release.yml` then:

1. Reads the top `## [x.y.z] - YYYY-MM-DD` heading of `CHANGELOG.md` that is not `[Unreleased]`, and skips if `v<version>` is already tagged.
2. Stops if `aimbuddy.versionName` does not match that heading.
3. Builds the release APK and the debug APK on `ubuntu-latest` with JDK 17, Android SDK 35, NDK 29, CMake 3.22.1 and a Gradle cache.
4. Signs both with the upload keystore. These repository secrets are **required**, and the job fails rather than publishing an unsigned or debug-signed APK that players could not update over:
   - `KEYSTORE_BASE64` (base64 of the JKS upload keystore)
   - `KEYSTORE_PASSWORD`
   - `KEY_ALIAS`
   - `KEY_PASSWORD`
5. Runs `.github/scripts/check-apks.sh`, which stops the release unless both signatures verify, both APKs use the same key, that key is the one the previous release was signed with, each APK has the right package and version, and neither has a detection model in it. The job summary shows the signer's SHA-256.
6. Creates the GitHub release `v<version>` on the commit it built. The notes are the changelog section followed by `.github/release-footer.md`. Two files are attached:
   - `AimBuddy-v<version>.apk`, the app (`com.aimbuddy`).
   - `AimBuddy-v<version>-debug.apk`, a debuggable build that installs beside it as `com.aimbuddy.debug`. It is much larger, because it is not shrunk.

After it publishes, look at the result as a player would: download both files, run `apksigner verify --print-certs` on them, install the app over the previous release, and open both.

## How the app finds an update

The app reads the project's published releases from the GitHub API and offers the newest one that is newer than itself. From that release it takes the file for its own build: the app takes `AimBuddy-v<version>.apk` and a debug build takes `AimBuddy-v<version>-debug.apk`, because each can only be replaced by a build of its own package. A pre-release is only offered to someone who already runs a pre-release, so a stable install is never moved onto a beta.

Before anything is installed the app checks that the download has the SHA-256 GitHub lists for it, that it is a newer build of the same app, and that it is signed with the same certificate as the installed one. When the certificate is different the app says so and links to the releases page, because Android will not replace one with the other and the only way on is to uninstall first. The system's installer checks the signature again. This is why every release has to use the same signing key.

The model store is separate: it reads `model/index.json` from the `master` branch, so models can be added or updated without a release. See [model/README.md](../model/README.md).

## Release signing

Android identifies an installed app by the pair (`applicationId`, signing
certificate). Every published AimBuddy APK must therefore be signed with one
fixed key. If the certificate changes between releases, users cannot upgrade
and the installer reports a package conflict.

0.3.0-beta.2, 0.3.0-beta.3 and every release after them are signed with the certificate whose
SHA-256 is `140ed02ffe4830211fdb61da47eba26e508aa67e88262e7f66ecb1a3d57eb675`.
0.3.0-beta.1 was published unsigned, so a copy of it was signed by whoever installed it.

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

With it, `assembleRelease` signs with the upload key, and so does `assembleDebug`:
the debug build uses the key too whenever the keystore is at hand, so that the
debug files published with releases can update one another. Without a keystore,
a local release build is signed with Android's debug key and must not be
distributed, and the debug build keeps that key.

**CI.** The workflow feeds the same key to Gradle through
`AIMBUDDY_KEYSTORE_FILE`, `AIMBUDDY_KEYSTORE_PASSWORD`, `AIMBUDDY_KEY_ALIAS`,
and `AIMBUDDY_KEY_PASSWORD`, populated from the repository secrets listed above.
Base64-encode the keystore for the `KEYSTORE_BASE64` secret:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("upload.jks")) | Set-Clipboard
```

`check-apks.sh` downloads the previous release's APK and compares its signer
with the new one. A different signer stops the release.

## Installing a build by hand

```powershell
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Which copy can be replaced by which:

| Installed | New | Result |
|-----------|-----|--------|
| A release from 0.3.0-beta.2 on | A later release | Installs over it and keeps the settings. |
| `com.aimbuddy.debug` from a release | The debug file of a later release | Installs over it. |
| `com.aimbuddy.debug` built on your computer | A debug file from a release (or another computer's build) | Package conflict: each was signed with a different key. |
| `com.aimbuddy` | `com.aimbuddy.debug` | No conflict: they are two apps and install side by side. |

If an install fails with **"App not installed as package conflicts with an
existing package"**, a build of the same package signed with a *different*
certificate is still registered on the device. Remove it for every user profile
and retry:

```powershell
adb uninstall com.aimbuddy
adb shell pm uninstall --user 0 com.aimbuddy
adb shell pm list packages -u | findstr aimbuddy   REM should print nothing
```

The last command also lists *uninstalled-but-retained* packages, which are the
usual cause when you are sure the app is not installed. Use `com.aimbuddy.debug`
in place of `com.aimbuddy` for the debug build.
