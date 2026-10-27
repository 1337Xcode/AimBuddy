# AimBuddy

An on-device computer vision and control system that helps people aim in touchscreen 3D shooters. It finds people in the game picture with a neural network, outlines them, and can steer the view toward the nearest one with one extra finger. It is built for players with low vision or motor impairments, for whom fast, precise aiming on a touchscreen is the barrier.

Android 11 or newer, arm64. Kotlin and Jetpack Compose for the app, C++17 for detection, tracking and control, NCNN for inference on the CPU or through Vulkan, Shizuku or root for touch access. Free and open source.

## What is technically different

**The aim controller does not know the game's camera, and learns it while it aims.** How far a game's view turns per pixel of finger movement differs between games and sensitivity settings by a factor of ten or more, and the game reacts a frame or two late. A controller that assumes either figure overshoots or circles the target. `AimController` records every move it makes, `GlobalMotion` measures how the whole picture slid between frames, and `ResponseEstimator` fits the camera's gain and delay from the two by least squares. Ordinary aiming is the experiment, and nothing is calibrated by hand. Until the estimate can be trusted, only a little finger travel is allowed to be in flight, so the first move is small and its effect teaches the real figure. The controller aims at where the target will be when its move takes effect, takes its own strength down if the aim starts to swing, and lifts and re-places the finger when the game does not answer it.

**The camera is read from video.** `GlobalMotion` reduces two frames to 64 by 64 grey values and finds the picture's shift by phase correlation, then polishes the strongest peaks with a few least-squares steps. Parts of the picture that do not move with the camera (the game's buttons and crosshair, the weapon, this app's own overlay) are learned and left out. So are detected people: a large one crossing the middle of the view would otherwise be mistaken for the camera turning.

**A second finger on a screen that Android will not share.** The input dispatcher follows one input device at a time in a window, so a second virtual touchscreen next to the real one loses its moves whenever a real finger is down. The touch helper (`libaimtouch.so`, run through Shizuku or root) instead takes over the real touchscreen, passes every event to one virtual copy of it, and adds the assist's finger to that copy as an extra contact. The game sees one screen with the player's fingers plus one. The helper takes the screen only when no finger is on it, needs a heartbeat from the app, gives the screen back when both volume keys are held, and if it is killed the kernel gives the screen back by itself. The merge logic (`TouchMux`) performs no input or output, so it is tested on a computer.

**Detection on a phone that is also running the game.** The detector reads only a square around the crosshair, copied out of the capture buffer inside the callback. The newest frame wins: a slow phone drops frames instead of falling behind, so a result is never older than one detection. At start the model is timed on the CPU and on the GPU, and the GPU is used only if it is clearly faster, because the game needs it too. CPU work stays on the fast cores, and detection is cut to fewer runs a second when Android reports that the phone is getting hot. The tracker is a Kalman filter with two-pass association in the manner of ByteTrack, and its speed estimate is free of the camera's own movement.

**An overlay that Android's rules do not break.** From Android 12 the system drops every touch that passes through another app's overlay that is more opaque than 0.8, and it judges a `SurfaceView` by a rule that ignores transparent pixels. The overlay is therefore drawn in a `TextureView` in a window kept just under the limit, and it becomes opaque only while its menu takes touches. A window marked secure is drawn as solid black in every capture that is not secure, and that includes the app's own capture of the screen. So streamer mode draws nothing over the game and makes the window secure only while the menu is open.

**An app that updates itself, and the models it runs.** The app reads the project's GitHub releases and downloads a newer APK. It installs it only after checking the SHA-256, the package name, that the version is newer, and that the signing certificate is the one already installed. Models come from a static index in this repository: every file is checked against the size and SHA-256 the index gives, a new model replaces the old one only when it is complete, and the default model is also built into the APK so a first run needs no connection. See [model/README.md](model/README.md).

## How a frame becomes a touch

```
screen capture -> square around the crosshair -> detector (CPU or GPU)
  -> camera motion (phase correlation) -> tracker (Kalman, two-pass matching)
  -> aim controller (delay compensated, learns the camera) -> touch helper
  -> virtual touchscreen with one extra finger
tracker -> overlay (outlines, drawn a little ahead of now)
```

[docs/Architecture.md](docs/Architecture.md) follows each step through the code.

## How it is verified

- `app/src/test/native` holds C++ tests of the parts that perform no input or output: the tracker, camera motion, the touch merger, the detector's decoding and the settings. The aim controller is run against a simulated game with a known camera gain and delay, and has to settle for gains and delays it was not told.
- `app/src/test/java` holds Kotlin tests of the update logic, the model store and the thermal limits.
- Debug builds include a stand-in first-person game with a known camera response, used to run the whole loop on an emulator.

## Install

1. Download the latest APK from [Releases](https://github.com/1337Xcode/AimBuddy/releases) and open it.
2. Open AimBuddy. It opens on the Models page: tap Get on AimBuddy General, or import a model of your own. The app carries no model, so this comes first.
3. When it asks, allow display over other apps, notifications and screen capture.
4. For aim assist, give AimBuddy touch access with [Shizuku](docs/ShizukuSetup.md) (no root needed) or with root. Without either, AimBuddy still outlines people.
5. Press Start, then switch to your game.

The first release that can update itself has to be installed by hand once; after that updates arrive in the app.

## Build from source

You need JDK 17, the Android SDK 35 with build tools 36, NDK 29.0.13113456 and CMake 3.22.1.

```sh
./gradlew assembleDebug          # app/build/outputs/apk/debug/app-debug.apk
./gradlew testDebugUnitTest      # Kotlin tests
sh app/src/test/native/run.sh    # C++ core tests, with a C++17 compiler on the path
```

Each build generates its own launcher icon from a random seed; `-Paimbuddy.iconSeed=<number>` repeats one. Releases are described in [docs/Releasing.md](docs/Releasing.md).

## Documentation

| Document | Contents |
|----------|----------|
| [Architecture](docs/Architecture.md) | How the app is put together, from capture to touch |
| [Settings guide](docs/SettingsGuide.md) | Every setting and what it does |
| [Shizuku setup](docs/ShizukuSetup.md) | Touch access without root |
| [Performance](docs/Performance.md) | What costs time, and how to read the numbers |
| [Training](docs/Training.md) | Training a detector for your game |
| [Troubleshooting](docs/Troubleshooting.md) | When something does not work |
| [Releasing](docs/Releasing.md) | Building, signing and publishing, and how updates reach players |
| [Model store](model/README.md) | How models are published and installed |
| [Changelog](CHANGELOG.md) | What changed in each release |
| [Contributing](CONTRIBUTING.md) | Standards and the pull request process |

## Credits

[NCNN](https://github.com/Tencent/ncnn) runs the model and [Dear ImGui](https://github.com/ocornut/imgui) draws the in-game menu. [Shizuku](https://github.com/RikkaApps/Shizuku) provides touch access without root. The app is built with Jetpack Compose and the Geist font. The detection models are made with [Ultralytics](https://github.com/ultralytics/ultralytics) YOLO. The full list, with licenses, is under Settings, About, Open source notices.

## License

AimBuddy is released under the AimBuddy Community Free Use License v1.0. In short: use it, change it and share it freely; do not sell it; anything you build from it stays free under the same terms and says "Based on AimBuddy" with a link here. See [LICENSE](LICENSE) for the full terms.

The YOLO26 detector that comes with the app is made by Ultralytics and is licensed under AGPL-3.0.

## Legal notice

AimBuddy is aim assistance software. Use it only where local law, the game's rules and the platform's terms allow it. Some games forbid aim assistance in online play, and using it there can get an account banned. You are responsible for how you use it.
