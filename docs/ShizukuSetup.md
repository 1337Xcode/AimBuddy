# Shizuku setup

Shizuku gives AimBuddy touch access without root. This guide gets Shizuku running and connected to AimBuddy. If your phone is rooted you can skip it: AimBuddy asks your root manager instead.

The first time takes about five minutes. After that, one step each time the phone restarts.

## Why AimBuddy needs it

Android does not let a normal app touch the screen on behalf of another app. For the aim assist, AimBuddy starts a small helper program. The helper takes the touchscreen over, passes every touch of yours on unchanged to a virtual copy of the screen, and adds one more finger to that copy: the assist. Starting such a helper needs more privilege than a normal app has, and Shizuku or root can provide it.

Shizuku is an app that you start once after each reboot, using Android's own wireless debugging or a computer. It then lends that privilege to the apps you allow. Without Shizuku or root, AimBuddy still outlines enemies. Only the aim assist is unavailable.

## What you need

- An Android phone, version 11 or newer.
- For wireless debugging, a Wi-Fi connection. For the computer route, a USB cable and a computer.

## Step 1: install Shizuku

| Source | How |
|---|---|
| Google Play | Search for "Shizuku" by Rikka and install it. |
| GitHub | Download the latest APK from [the Shizuku releases](https://github.com/RikkaApps/Shizuku/releases) and install it. |

Open Shizuku once. It says it is not running. That is normal; start it with one of the routes below.

## Step 2: start Shizuku

Pick one.

### Wireless debugging, no computer

1. Open Settings, About phone, and tap Build number seven times. A message says you are a developer.
2. Open Settings, System, Developer options, and turn on Wireless debugging. The phone has to be on Wi-Fi.
3. Open Shizuku, choose to start it with wireless debugging, and begin the pairing.
4. In the system's Wireless debugging screen, tap Pair device with pairing code. Pull down the notification shade and type the code into the notification that Shizuku shows.
5. In Shizuku, tap Start. When it says Shizuku is running, you are done.

Pairing is needed once. If Shizuku does not start, turn Wireless debugging off and on again and tap Start again.

### A computer and a USB cable

1. Install [platform-tools](https://developer.android.com/tools/releases/platform-tools) on the computer.
2. Turn on USB debugging in Developer options, plug the phone in, and accept the "Allow USB debugging?" prompt.
3. Check that the phone is listed and is not "unauthorized":

   ```sh
   adb devices
   ```

4. Start Shizuku:

   ```sh
   adb shell sh /sdcard/Android/data/moe.shizuku.privileged.api/start.sh
   ```

   If that path is not found, the Shizuku app shows the exact command for your install.

### A rooted phone

Open Shizuku and use its Start button for root.

## Step 3: let AimBuddy use it

1. Open AimBuddy and press Start.
2. Answer what it asks, in this order: display over other apps, notifications (on Android 13 and newer), Shizuku's own request to allow AimBuddy, and screen capture.
3. When AimBuddy is running, the Touch access row on the Home screen says Shizuku.

Shizuku remembers the permission, so you only answer its request once.

## After a restart

Shizuku stops when the phone restarts. Start it again with the route you used before: for wireless debugging, turn Wireless debugging on, open Shizuku and tap Start. On some phones wireless debugging turns itself off or forgets the pairing after a restart. If so, pair again.

## If something does not work

| What you see | What to do |
|---|---|
| Touch access says Not set up, or "Aim assist needs Shizuku or root" | Shizuku is not running or has not been allowed. Open Shizuku. If it says it is not running, repeat Step 2. |
| "Shizuku was not allowed. Targets are still highlighted." | Open Shizuku, go to the list of authorized apps, and allow AimBuddy. Then press Start again. |
| No Shizuku request appears | Force-stop Shizuku and AimBuddy, open Shizuku, check that it is running, and press Start in AimBuddy again. |
| Shizuku stops by itself during play | The phone's battery manager is killing it. Exclude Shizuku and AimBuddy from battery optimization, see below. |
| The touchscreen misbehaves | Hold both volume keys: the helper gives the touchscreen back and stops. Or use Stop in the notification. |

### Battery optimization

| Maker | Menu |
|---|---|
| Samsung | Settings, Apps, Shizuku, Battery, Unrestricted |
| Xiaomi (HyperOS, MIUI) | Settings, Apps, Manage apps, Shizuku, Battery saver, No restrictions; and turn Autostart on |
| OnePlus, Oppo | Settings, Battery, Battery optimization, Shizuku, Don't optimize |
| Stock Android | Settings, Apps, Shizuku, Battery, Unrestricted |

Do the same for AimBuddy, or the system may stop it in the middle of a session.

## What the helper does to your touchscreen

The helper takes the touchscreen over only when no finger is on it, and gives it back when AimBuddy stops, when you hold both volume keys, and if the helper itself is killed. [Architecture](Architecture.md) describes how, and [Troubleshooting](Troubleshooting.md) covers problems that are not specific to Shizuku.

For everything else about Shizuku, see [its documentation](https://shizuku.rikka.app/).
