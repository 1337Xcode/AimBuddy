# Changelog

All notable changes to AimBuddy. Format inspired by [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Dates are in ISO-8601 (YYYY-MM-DD).

## [Unreleased]

## [0.3.5-beta.1] - 2026-10-28

A rebuilt app with a model store and updates from inside the app. The APK no longer carries a model, and a debug build is published beside the release one.

### Added
- Updates from inside the app. It reads the project's releases on GitHub and offers a newer one on the Home screen and in Settings. Before it installs anything it checks the SHA-256, the package name, that the version is newer and that the signing certificate matches the installed copy, then hands the file to Android's installer. If an update is signed with another key, the app says so and links to the releases page. This is the first release that can do this, so it has to be installed by hand once.
- A model store. `model/index.json` lists the models in the `model` folder and the Models screen installs them. Each file is checked against its size and SHA-256, the old model stays until the new one is complete, and updates are offered. AimBuddy General is published there. A first run opens on the Models page with a short guide to downloading a model or importing your own; when the first one is in, the page says so and points back to Start. Pressing Start with no model leads there too.
- A touch helper, `libaimtouch.so`, started through Shizuku or root. It takes the real touchscreen over, passes your touches to a virtual copy and adds the assist's finger as one more contact. It takes the screen only when no finger is on it, needs a heartbeat from the app, and gives the screen back when both volume keys are held or when it dies.
- Measuring the game's camera while you play: how far the picture slid between frames, and from that the camera's gain and delay. The aim controller plans through that delay, starts with small moves until the camera is known, and lifts and puts the finger down again when the game ignores it.
- Ten box colors, ten accent colors, a performance readout, a Menu button in the notification, and a notice on the first run that the app is free and open source.
- Two outlines for the fields of view: a blue square for the area the detector looks at and a red circle for how far the assist reaches. They are drawn while you play, each with a switch in the app and in the in-game menu, and always while the menu is open.
- Detection eases off when the phone reports that it is getting hot: at most 30 detections a second when it is warm and 15 when it is hot, and full speed again once it has cooled down.
- A launcher icon made of TV static, edge to edge in every layer, so it fills whatever shape the launcher cuts. Each build draws its own (`-Paimbuddy.iconSeed` repeats one). The floating button is a bubble of the same static, drawn anew each time it appears.
- A debug APK beside the release one on every release, `AimBuddy-v<version>-debug.apk`. It installs as a separate app (`com.aimbuddy.debug`), is signed with the same key, and updates itself from the debug file of later releases.
- Labelling training frames with a model made for your game and exported to ONNX (`--teacher-onnx`). The model reads the frame in tiles at the pixel size it was trained on.
- Tests for the tracker, the aim controller against a simulated game and a simulated person steering by hand, camera motion, the touch merger, the detector's decoding, the settings, the update logic and the model store.

### Changed
- Every screen is rebuilt in Compose, with a top bar that carries the screen's title, settings in groups, tiles for the models and no splash screen.
- Detection reads only a square around the crosshair, on the CPU or the GPU, whichever is faster on the phone, and the tracker follows targets through missed detections.
- Settings live in one text file, `settings.cfg`, and are saved a moment after each change. The assist starts off on every run.
- Streamer mode draws nothing over the game and makes the floating button invisible, and the menu is secure only while it is open. The old design blacked out the game in recordings.
- The overlay draws in a `TextureView`.
- The app carries no model. The release APK is 7.2 MB, down from 12.7 MB, and AimBuddy General (9.2 MB) is downloaded from the store the first time.
- Debug builds install beside the release build as `com.aimbuddy.debug`.
- The release workflow builds both APKs, signs them with the upload key and checks them before it publishes: that the signatures verify, that both use the key of the previous release, that each has the right package and version, and that neither carries a model.
- The training pipeline writes `model.param`, `model.bin` and `model.json` to `training/outputs/export/` and no longer writes into the app's assets.
- The documentation is rewritten to match.

### Removed
- The old aim settings: presets, stabilization filters, velocity lead, anti overshoot and damping. The closed loop controller needs none of them.
- The old ESP renderer and overlay window.

### Fixed
- Pressing Stop soon after Start froze the app until the CPU and GPU timing that runs at the start had finished. On an emulator that took 54 seconds and Android showed the app as not responding. Stop now ends the timing at its next step, and a Vulkan device that is only the CPU (an emulator's) is left out of it, which brings the timing there down to 3 seconds.
- Boxes trailed the targets while the player turned the camera, by about 45 px in a simulated match. The camera is now read on the captured frames up to 30 times a second, not only on the frames the detector looks at, and the player's own turn is carried a little past the newest frame. The same match now shows about 7 px.
- Ghost boxes: a box the detector reported for a single frame was drawn for a moment. A new target is now drawn from its third sighting on, or at once when the detector is at least 90 percent sure.
- Boxes of still targets wandered about 6 px between screen refreshes. They are smoothed harder now (about 3 px), and only the part of a target's speed the tracker is sure of is carried forward.
- The assist stayed timid whenever the player turned the camera by hand: it could not measure how far the game's camera moves per finger pixel, found a gain and delay that were wrong with a confidence of about a third, and was therefore allowed only small moves. In 60 simulated matches with a swinging player it now finds the gain within 15 percent in 57 of them (23 percent off at worst) and the delay within 5 ms. Before, it found the gain within 15 percent in 1 of the 60, and was 88 percent off at worst.
- Boxes came and went because the app's own outlines changed what the detector saw: the overlay is part of the captured picture, and on frames of a free shooter a full outline close to a target moved the model's confidence in it by 0.28 on average and lost one target in five. Targets are now marked with corner brackets a good way off them, which moved it by 0.09 to 0.13 and lost none (ten boxes, measured offline).
- The assist chased a box stuck to the screen (the player's own weapon or character taken for an enemy) for ever: in a simulated match it turned the camera more than 13,000 px in 12 seconds and lost the real enemy. It now gives up on such a target for a few seconds, longer each time, and the overlay stops drawing the box.
- Touches did not reach the game through the overlay on Android 12 and newer.
- The app crashed when the touch helper's output was cut short.
- The first assist touch after the touchscreen changed hands was ignored by the system. The app now waits 700 ms.
- A model that fails to download, or a damaged model list, no longer harms the model in use.
- A model's description in the store was cut off after two lines. It now gets three.
- Chinese text used half-width punctuation.

### Upgrade note
- This release installs over beta 2 and beta 3 without uninstalling, because all three are signed with the same key. The settings were rebuilt, so they start at their defaults. The app no longer carries a model, so after the update it opens on the Models page once, where you download AimBuddy General or import your own.

---

## [0.3.0-beta.3] - 2026-08-09

Release signing fix, correct in-app version reporting, and a 61% smaller APK.

### Fixed
- Installing a release APK failed with "App not installed as package conflicts with an existing package". The release build type was signed with `signingConfigs.debug`, whose keystore is per-machine and is regenerated from scratch on a clean CI runner, so every build carried a different signing certificate. Android keys an installed app on (`applicationId`, certificate), so no two AimBuddy APKs could be installed over one another. Release builds now use a dedicated, stable keystore supplied through `keystore.properties` or the `AIMBUDDY_KEYSTORE_*` environment variables.
- The ImGui window title and the Info tab reported `v0.0.0-dev` in every build. Gradle passed `AIMBUDDY_VERSION` to CMake as a cache variable, which the compiler never sees, and `CMakeLists.txt` never promoted it to a preprocessor define, so `utils/i18n.h` always fell through to its default. CMake now defines it for the compiler and warns if Gradle fails to supply it.

### Performance
- Release APK shrank from 32.59 MB to 12.66 MB. `proguard-rules.pro` carried a blanket `-keep class androidx.compose.** { *; }`, which disabled R8 shrinking across every Compose artifact and pinned the whole of `material-icons-extended` into the dex. The app references nine icons; dex dropped from 21.64 MB to 2.07 MB with all nine retained.
- Dropped v1 (JAR) signing. It is only consulted below API 24 and `minSdk` is 30, so its per-entry digests in `MANIFEST.MF` and `CERT.SF` were roughly 93 KB of dead weight. v2 and v3 remain enabled.

### Changed
- The release workflow now requires the signing secrets and fails instead of publishing an unsigned or debug-signed APK, verifies the APK signature after building, and prints the signer's SHA-256 fingerprint to the job summary so certificate drift is caught before release.
- Release assets are named `AimBuddy-v<version>.apk`; the misleading `-signed`/`-unsigned` suffix is gone.
- Local release builds without a configured keystore still fall back to the debug key for convenience, but now warn that the APK must not be distributed; the same fallback is a hard error when `CI` is set.

### Upgrade note
- Because earlier APKs were signed with throwaway keys, this release cannot be installed over an existing AimBuddy build. Uninstall the old version first (`adb uninstall com.aimbuddy`, or long-press the app icon → Uninstall). Subsequent releases will upgrade in place.

---

## [0.3.0-beta.2] - 2026-06-04

UX overhaul and critical APK installation fix.

### Added
- Premium Compose-based Model Store UI replacing legacy Alert Dialogs. Features tabs for browsing available models from GitHub and managing installed models with options to download, switch, and delete models.
- Support for deleting downloaded/imported model files through the UI.

### Fixed
- Fixed immersive landscape layout breaking/shifting by implementing an in-window animated menu overlay for the 3-dot dropdown, avoiding system bar visibility toggles caused by system PopupWindows.
- Fixed APK installation failure ("package is invalid") on modern Android devices by automatically signing release builds using the debug signing configuration when no custom release keystore is provided.

### Changed
- Redesigned home screen into a landscape-optimized side-by-side panel layout, grouping Status and Active Model information on the left and primary actions with input backend chips on the right.
- Enhanced launcher status reporting by mapping internal JNI status strings to professional, descriptive titles and subtitles (e.g., translating "Model Loading" to "Initializing Model" with detailed buffer allocation details) for better user feedback.

---

## [0.3.0-beta.1] - 2026-05-31

Tooling, docs, and release-automation pass on top of 0.2.0-beta.1.

### Added
- Beginner-friendly Shizuku setup guide at `docs/ShizukuSetup.md` covering wireless-debugging pairing, ADB start, root start, permission grant, post-reboot behavior, OEM battery-saver workarounds, and troubleshooting.
- GitHub Actions release workflow at `.github/workflows/release.yml`. Triggered on every push to `master` that modifies `CHANGELOG.md`; reads the top non-Unreleased version heading, verifies it matches the `aimbuddy.versionName` Gradle property and is not already tagged, builds the release APK with the Android SDK action, optionally signs with a keystore from repository secrets, and publishes a GitHub Release with the changelog section as the body and the APK attached.
- Centralized version metadata. `aimbuddy.versionName` and `aimbuddy.versionCode` live in the root `gradle.properties` and are consumed by `app/build.gradle` (APK metadata + `BuildConfig.AIMBUDDY_VERSION`), by the native side through a new `-DAIMBUDDY_VERSION=...` CMake define, and by the release workflow when validating that the changelog matches the build. The ImGui menu title and Info tab now read from that single source rather than hardcoded literals.

### Changed
- Updated mermaid diagrams across `docs/Architecture.md`, `docs/Performance.md`, and `docs/Training.md` to reflect the 4-slot ring buffer, event-driven aim loop, Shizuku permission branch, and the new `automate.py` end-to-end pipeline.

---

## [0.2.0-beta.1] - 2026-05-29

Major feature release. Touch parity, streamer mode, Chinese UI, redesigned launcher, full training-pipeline overhaul, and a significant runtime memory/CPU pass.

### Added
- Streamer mode toggle in the ESP tab that sets `FLAG_SECURE` on the overlay and floating-icon windows and calls `SurfaceView.setSecure(true)` on the GL surface so the overlay is stripped from MediaProjection, screen recorders, screenshots, and mirroring while still visible on the user's own screen.
- Chinese (`中文`) UI language alongside English. New `utils/i18n.h` holds the per-key translation table. CJK font loading tries `/system/fonts/NotoSansCJK-Regular.ttc` and other vendor fallbacks first (works on every modern Android with no APK bloat), then falls back to `assets/fonts/cjk.ttf` if a user-bundled font is present. Language is persisted in `UnifiedSettings.language` and chosen from the ESP tab.
- Redesigned launcher screen built on Material3: `CenterAlignedTopAppBar` with an overflow menu (Import model / Model store / GitHub), a status card with a colored state pill (grey idle, amber waiting, green running, red error), backend-readiness `AssistChip`s for Root and Shizuku, and a single primary `FilledTonalButton` that toggles between Start and Stop. New dark palette tuned for the launcher.
- Teacher-driven auto-labelling pipeline (`training/src/auto_label.py` + `scripts/08_auto_label.bat`) that runs a large COCO-pretrained YOLO (default `yolov8x.pt` at `imgsz=1280`) over `raw_frames/` and emits YOLO-format enemy labels - replaces hours of manual box-drawing with minutes of spot-checking.
- Hard-negative mining (`training/src/mine_negatives.py` + `scripts/09_mine_negatives.bat`) for empty-label samples from `raw_frames/negatives/`, used to suppress false positives on menus, vehicles, friendly NPCs, etc.
- Active-learning sweep (`training/src/active_learning.py` + `scripts/10_active_learning.bat`) that scores unlabelled frames by uncertainty and teacher-student disagreement and surfaces the top-N for review.
- Stable hash-based train/valid/test split (`training/src/split_dataset.py`) that never moves a frame between splits, so re-running the pipeline as new data lands stays leak-free.
- End-to-end automation script (`training/src/automate.py` + `scripts/00_automate.bat`) that runs setup -> extract -> auto-label -> mine negatives -> split -> validate -> train -> NCNN export -> active-learning, checkpointing to `outputs/reports/automate_state.json` so a re-run resumes from the last failure.
- Separate train-time and runtime image sizes: train at `imgsz=640` for distant-target recall, export NCNN at `runtime_imgsz=256` for mobile latency. New `[augmentation]` block in `config/config.ini` (mosaic, mixup, copy-paste, HSV, perspective) so the model generalizes across maps/outfits/poses.

### Changed
- Touch backends rebuilt for true user+aim parallelism:
  - **uinput (root):** removed `EVIOCGRAB` on the real touchscreen and the reader thread that mirrored events back through uinput. The aimbot now owns a separate `aimbuddy-virtual-touch` device with its own slot 0 and tracking id (`0x7000`); the kernel reports both devices to InputDispatcher, so finger and aim contact dispatch as parallel streams.
  - **Shizuku (non-root):** synthetic `MotionEvent` carries `deviceId = -1` and pointer id `19`, keeps its own `downTime`, and uses `INJECT_MODE_ASYNC` so InputDispatcher routes the synthetic gesture as a parallel stream rather than merging it into the user's pointer history.
- Aim controller smoothed for less shake: heavier blend when locked, cubic ramp-to-zero below 2 px, derivative gain attenuated near lock, removed `usleep()` calls from `applyMovement`. Velocity-lead lookahead now scales with measured pipeline delay (capped at 80 ms) and the default `velocityLeadFactor`/`velocityLeadClamp` were tripled so running targets are actually predicted.
- Aim loop is now event-driven: condition-variable wakeup from `updateTargets()` triggers the first touch within one inference cycle of acquisition; sleeps for 120 ms when the aimbot is disabled instead of polling.
- Target tracker resets EMA/Kalman filter state when a track is re-acquired after being lost, eliminating ghost-box drag.
- Runtime crop ceiling reduced from 480 px to **320 px** (`Config::CROP_SIZE`). Smaller resize ratio into the model (1.25x vs 1.875x) cuts per-frame CPU; the smaller FOV also matches what `imgsz=256` actually resolves cleanly.
- Frame ring buffer reduced from 8 slots to **4 slots** (`Config::RING_BUFFER_CAPACITY`). Halves peak AHardwareBuffer footprint (~28 MB -> ~14 MB). The inference loop drains-to-latest each iteration, so deeper buffering only added latency.
- ImGui overlay render now capped to 60 FPS (90 FPS while the menu is open), preventing 120/144 Hz panels from redrawing the overlay twice per inference frame.
- Frame extraction default `--source-crop` changed from 480 to 320 to keep train/runtime preprocessing geometrically identical.
- Stronger training augmentations enabled by default (`mosaic=1.0`, `mixup=0.15`, `copy_paste=0.30`, wider `scale=0.6`, larger `hsv_v=0.45`); `close_mosaic` bumped to 15 so mosaic stays active longer in the schedule.
- ImGui Aim/ESP/Info tab labels, presets, sliders, and footer translated through `T(Key::...)`; per-frame language sync so a toggle takes effect immediately.

### Fixed
- User finger + aimbot touch can now run simultaneously on both backends - previously one would cancel or replace the other depending on backend.
- Aim no longer flicks toward stale "ghost" boxes after target re-acquisition.
- `usleep()` calls inside the aim loop no longer stall the controller mid-move.

### Docs
- `CHANGELOG.md` (this file).
- `docs/Architecture.md`: rewrote the Input Injection section to describe the parallel-stream design; added Streamer Mode section; updated the threading diagram to event-driven aim loop.
- `docs/Performance.md`: documented the 320 px crop ceiling, 4-slot ring buffer, runtime memory budget, and overlay render cap.
- `docs/SettingsGuide.md`: updated lead defaults; documented `streamerMode`; refreshed preset tables.
- `docs/Training.md`: added "Skipping Manual Labelling" section covering teacher auto-labelling, negative mining, active learning, and the high-train / low-runtime imgsz split.
- `app/src/main/assets/fonts/README.md`: documents the system-font-first CJK loading strategy and when a user would need to drop their own font.

---

## [0.1.0-beta.1] - 2026-04-25

Public-facing beta tag - corresponds to `bf93b26 feat: add Shizuku support for non-root input backend and enhance touch handling`. This is the first build that runs end-to-end on devices without root.

### Added
- Shizuku non-root input backend (`ShizukuInputInjector.kt`) using the hidden `IInputManager.injectInputEvent` API surfaced through Shizuku's binder wrapper. Adds the second supported touch path beside the root/uinput one.
- Backend toggle in the Aim tab and persisted `touchBackend` setting (0 = uinput, 1 = Shizuku).
- Permission flow: `requestShizukuThenMediaProjection()` mirrors the existing root flow, with binder listeners that auto-detect connect/disconnect.

### Changed
- Touch handler abstracted behind `TouchHelper::setBackend()` so the native aim controller calls a single API and `TouchHelper` dispatches to uinput or the Shizuku JNI bridge.

---

## [0.1.0-alpha.4] - 2026-04-16

Maps to `6223d76 feat: implement model store functionality with GitHub integration and local model management`.

### Added
- In-app model store (`ModelStoreRepository.kt`) that fetches model metadata from a GitHub repo branch, lists available models with size/description, and downloads/unpacks them into `filesDir/models/<id>/`.
- Installed-model catalog (`ModelCatalog.kt`) that tracks bundled, locally-imported, and store-downloaded models; the active model selection is reflected in the main-activity status text.
- Switcher dialog to choose between installed models, with hot-reload via `nativeShutdown` + `nativeInit` when the app is idle.

### Changed
- `nativeSetModelPaths(param, bin)` lets the native detector pick up store/local model files instead of always loading from assets.

---

## [0.1.0-alpha.3] - 2026-04-07

Maps to `933f7d0 feat: enhance model loading and import functionality in MainActivity`.

### Added
- Local model import via `OpenDocument` activity - pick a `.param` then the matching `.bin` and they are copied into the app's private models directory.
- Legacy single-import migration so users who imported a model under the older single-slot scheme are not stranded.

### Changed
- Status text now reflects the active model (`Model: <title> (<source>)`).

---

## [0.1.0-alpha.2] - 2026-04-06

Spans `eb21245`, `84e7627`, `a7b296d`, `b93d8c5` - overlay UX polish, GitHub Pages site, and richer docs.

### Added
- ESP overlay groundwork for the "menu visible" state machine - visibility check that suppresses aim while the menu is open.
- Initial GitHub Pages scaffolding under `site/` and a deploy workflow.

### Changed
- Improved enemy detection / menu-visibility checks in `eb21245` make the aim controller stop cleanly the moment the menu opens.
- Main layout dimensions updated to align the floating icon with the overlay coordinate space.

---

## [0.1.0-alpha.1] - 2026-03-25

Foundational milestone. Five commits land on a single day: `79438a8` (initial), `2669072` (root permission handling), `5f8ccbe` (menu UX + native stability), `9d2dd69` (base model download script + setup), `c0eaae7` (initial aimbot tuning + perf), `946f8de` (initial docs pass).

### Added
- Initial repository: Android app shell (Kotlin + Compose), native C++ runtime, NCNN-Vulkan detector wired to YOLOv26n, ESP overlay with ImGui, basic settings menu, target tracker, aim controller with smooth/snap/magnetic modes, uinput-based touch injection, MediaProjection capture pipeline, training Python pipeline (extract -> validate -> train -> export -> deploy).
- Hardened root permission handling with timeout (`RootUtils`).
- Polished menu UX (tabs, presets, save-on-edit) and native overlay stability fixes.
- Base model download script + setup instructions so `yolo26n.pt` is recoverable when missing.
- Initial documentation set: README, Architecture, Performance, SettingsGuide, Training, Troubleshooting, Contributing.
