### Downloads

- `AimBuddy-v{{VERSION}}.apk` is the app. Install this one.
- `AimBuddy-v{{VERSION}}-debug.apk` is a debuggable build for testing and bug reports. It installs beside the app as a separate app (`com.aimbuddy.debug`) and does not replace it.

Neither file contains a detection model. The app opens on the Models page, where you download one or import your own.

If Android says "App not installed as package conflicts with an existing package", the copy on your phone was signed with a different key. Uninstall it once, then install again. Releases from beta 2 on are signed with the same key, so they install over each other.
