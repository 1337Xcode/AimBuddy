# Changelog

All notable changes to AimBuddy. Format inspired by [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Dates are in ISO-8601 (YYYY-MM-DD).

## [Unreleased]

## [0.3.5-beta.2] - 2026-10-29

Colour detection that needs no model, two choices for what steering does on a target, a Touch access screen with a test button, and the fixes that five independent reviews of the code turned up.

### Added
- Two aim choices, in the app's Settings and in the in-game menu (English, Chinese and Russian). "Once on target" is what steering does when the crosshair is on a target: keep following it, as before, or make one correction, lift the finger and leave the aiming to the player until the next target (or until the same one has left the reach and come back). In the simulation the one correction landed on a still target 300 px away in under a second, and it did not follow a target swinging 140 px to either side afterwards, which is the point of it. "Switching targets" is how readily the lock moves to a target that has come closer to the crosshair: Sticky (the rival must be 45% closer and stay so for 0.3 s), Balanced (30% closer for 0.15 s, as before) or Quick (15% closer for 0.06 s, which moves from one player to the next almost at once).
- Colour detection, a way to find targets with no model (experimental). Under Settings, Detection, choose Colour for Find targets with, and press Start. Open the in-game menu, press Learn colour and tap the colour of an enemy's outline inside the blue square. AimBuddy takes the most vivid colour round the tap and looks in the square for patches of that hue (the tolerance is 14 degrees and can be set from 4 to 40). It closes the small gaps that scenery makes in an outline, joins the head and the body of one player, and keeps the patches that are shaped like a person and at least 40 screen pixels tall (10 to 300 can be set). Every patch is reported at 90% and tracked like a model's box. The overlay turns its own marks away from the colour looked for, because they are in the picture too, and the performance readout says Colour where it says CPU or GPU. A colour that is also in the scenery or on the game's buttons gives false targets. On the development emulator the finder took 6.6 to 9.8 ms a frame over a minute, and its targets were 20 to 28 ms old when they were ready.
- A Touch access screen, opened from the Touch access row on Home and from Settings, Touch (English, Chinese and Russian). It holds the choice of route (Automatic, Shizuku or root), shows where Shizuku stands (Ready, Tap to allow, Not running or Not installed, each with the tap that does the next step) and whether root has been allowed, which is only known once it has been asked for, and has a Test touch access button. The test starts the touch helper by the chosen route the way Start does, says "Works through Shizuku" (or root) or why not, and stops the helper again; it is off while AimBuddy runs, and a press of Start is refused with a message until it has finished. It replaces the dialog that the Home row used to open, and the choice of route moved from Settings to this screen. On the emulator the test passed through Shizuku with the choice Automatic and with the choice Shizuku, said "Root was not granted" for root (the app is not allowed to run `su` there), and left no helper process behind.
- A download from the store shows how much has arrived, how fast and how long is left, and can be cancelled at any moment: the read ends at once, even on a connection that has gone quiet, and nothing is left behind. A download that fails says why under the model (no connection, a damaged file, no space) and offers Try again, where it used to say so once in a message that went away. When the store cannot be reached, the list saved by the last visit is shown with a note that it is old.
- A benchmark program, `sh app/src/test/native/bench/run.sh`. It times the per-frame steps (the colour finder, the camera look, the tracker, one step of the aim controller, the touch merger and the overlay's fingerprint) and runs the aim controller against simulated games whose camera gain and delay differ from what it was told. The README shows its figures.

### Changed
- Streamer mode no longer blacks out the whole screen while the menu is open. The secure window now covers only the menu's column of the display (see-through, as wide as the menu with a margin, as tall as the screen), and the full screen overlay window is never secure. On an emulator, a screenshot taken with the menu open was 49.5% solid black in landscape (2400x1080, in x 136 to 1320) and 80.4% in portrait (1080x2400, where the menu fills most of the width), and the rest was the game; the earlier design gave 97.3% black in portrait. With the menu closed nothing is black and nothing of AimBuddy is drawn: no boxes, no outlines for the fields of view, no touch area, no labels, no readout, no button. Streamer mode also no longer draws the guides while the menu is open, the menu cannot be resized in it, and "Move or resize on screen" and "Learn colour" are dimmed with a note, because they draw over the game and the colour is read from the screen. If the system refuses the secure window, the menu stays shut and a message says why. An open menu is closed when the screen turns.
- The detector gets a frame before the camera motion is measured on it, and no longer waits for it. The measurement (the camera look, at most 30 a second) ran on the capture thread before the frame was handed over, so every frame it was made on reached the detector late by as long as it took. On the emulator, in a scripted scene where the extra finger turns the camera the same way every time, the frame age (from the frame being on screen to its result being ready) was 37.3 and 32.4 ms in two runs before the change and 12.0 and 16.7 ms in two runs after it, while a look took 1.5 to 21 ms on average in those runs. The emulator runs the app's arm64 code through translation, so these are not a phone's figures.
- The camera look is about 28% faster. It compared the length of a gradient with a library call (`hypot`) where the squares give the same answer, and it read five neighbouring samples in each step of its refinement from five separate places where one set of pixels serves. Replaying 778 looks recorded on the emulator through the estimator on a desktop processor, fastest of seven passes for each: 0.84 ms on average before, 0.61 ms after, and the slowest 4.0 ms before and 2.2 ms after. The answers differ by at most 0.00001. The log line `Camera:` says how long a look took, on average and at most.
- The overlay leaves a frame out when it looks like the last one it drew. The screen capture only delivers a frame when the screen changes, and the overlay is on the screen, so an overlay that redrew itself 60 times a second kept the detector running on a still picture. On the emulator, with AimBuddy running over the app's own home screen, the app's threads used 214% and 203% of a core in two runs before the change and 46.6% and 2.6% in two runs after it (the readout was still settling in the first). A moving game is not affected: its frames change.
- The in-game menu was redrawn. Sliders show their name and value above a thin track, and a drop down list has its name above it. The tabs stay in view while a page scrolls, and a page scrolls when you drag a finger up or down on it (a finger that moves sideways still drags a slider). The page buttons are pills as wide as the page, and Close and Stop AimBuddy are two full width pills under a divider, Stop in red. The colour detection settings are in a card, and the prompt for learning a colour is a rounded bar in the menu's colours, with a dot that shows whether the colour was found.
- Settings fit more on a phone held sideways. In a window shorter than 480 dp the rows sit closer (a slider row is 72 dp tall instead of 87, a choice row 89 instead of 99, a switch row 49 instead of 56), the gap between groups is 21 dp instead of 34, and the top bar slides away as the page scrolls forward and comes back at the first swipe the other way. A window about 1100 dp wide or more gets a third column. The ends of a slider's track now sit under the ends of the text above it, where they had been 15 dp inside. Measured on screenshots of the emulator at 2400 by 1080 (the third column at 1200 dp wide); portrait is unchanged.

### Removed
- The Touch access list in the in-game menu. It changed a setting that is only read when AimBuddy starts, so it did nothing while AimBuddy ran. It is on the app's Touch access screen.

### Fixed
- Pressing Stop within a few seconds of Start crashed the app when Shizuku was running: Stop interrupts the thread that waits for Shizuku to start the touch helper, and nothing caught the interruption. The wait now gives up quietly and the stop finishes. Found in 0.3.5-beta.1.
- Start did nothing for a player who had said no to the notification permission twice: the app asked again as fast as the system answered (50 times in 4 seconds on an emulator) and never reached the screen capture step. It asks once for each press of Start now.
- A model whose description file was unreadable or gave a network input of an absurd size could take the app down while it started. A size outside 32 to 2048 pixels is now left for the detector to work out, an unreadable description is ignored, and a failure while the model or the touch helper starts ends in a message instead of a crash.
- The model store showed an older list, or said it could not be read, when the list had been fetched fine but the copy kept for offline use could not be written (a full phone). The fresh list is used and only the copy is skipped.
- A download and an import that finished at the same moment could leave one of the two models off the list, though its files were there. Changes to the list are made one at a time now; a test that adds 300 models from two threads loses some without it.
- Starting the touch helper through Shizuku again soon after it had been stopped could hang for the whole 10 second wait and fail (the second of two restarts 12 seconds apart did, on an emulator). Shizuku keeps one record for each service, and the new start joined the one that was still being taken down. Each start asks for a service of its own now.
- The Home screen went on showing the touch helper as connected after it had gone away (the system stopped it, the touchscreen's device vanished, or the player held both volume keys). The app now notices within half a second, starts the helper again up to three times a minute (not after the volume keys, which the player pressed on purpose), and then says why steering is not available. Killing the helper on an emulator three times in 40 seconds brought it back each time in about three seconds.
- A damaged line in `settings.cfg` cost its setting more than its value: an empty or text value was read as 0 (10% detection confidence, camera sensitivity 0.1, a switch turned off) instead of keeping the default, `nan` got through the range limits to the aim, and a number too big for its type wrapped round to a valid-looking one. Such lines are ignored now, and a file whose last write failed is no longer put in place of the good one.
- A game that took no notice of the extra finger was touched again and again when the finger kept reaching the edge of its touch area. Each new touch started the wait for an answer over, so the game was never judged to be ignoring it: in a 20 second simulation the finger went down 172 times and was never lifted for a wait. The wait is counted over the whole run of touches now, and the same run ends with 6 waits that grow and 90 touches.
- The training pipeline split its frames into train, validation and test by hashing each frame's name, so frames cut from the same video, which look almost the same, landed on both sides. The validation score and the choice of `best.pt` then measured how well the model remembered its training frames. The split hashes the name of the video now and keeps all of its frames together. A test with 200 videos of 6 frames each finds every video in one split (it does not with the old rule). A dataset that was split by frame before keeps the frames it has where they are, and the frames added later follow their video. With only a few videos the validation split can come out empty, because each video goes to one split: the script says so and stops (training needs a validation set), and `--by-frame` gives the old frame by frame split for a tiny dataset, with an optimistic score.
- Switching steering off and on again, or turning the screen, kept the wait the game had earned by ignoring the finger before. After four ignored fingers the next touch was two seconds away, and stayed so across the restart. A restart forgets it now.
- The performance readout went on showing the last detection rate after the screen stopped changing, and a pause pulled the average down only slowly (a 10 second pause counted as one reading of 0.1 a second among the 20 or so that make up the average). It reads 0 once no result has come for a second, and the average starts again with the next results. On the emulator the readout said `0/s` 25 seconds after AimBuddy was started over a still screen.
- Two equal frames of a picture with no edges in it (the sky, a plain wall, a view filled by one enemy) were taken as proof that the camera had not moved, with full trust. In front of a plain wall the camera could turn while the measured slide stayed zero, and the finger could be judged ignored for it. Such a pair of frames earns trust in proportion to the edges it holds now, and none when the picture is flat.
- A touch area placed near the edge of the screen could be larger than the room it had. The touch helper holds a finger at the edge when it is sent past it, while the controller counted the whole move, so the camera measurements and the check for an ignored finger saw a turn that never happened. The area is cut back to the nearest edge now, and the overlay and the editor draw the circle the controller really uses (76 px for an area at 93% of the width of a 1080 px screen).
- The controller pulled at full strength while the player turned the camera by hand, so the player's turn and the extra finger's pull added up. A setting for giving way already existed, but nothing told the controller when the player was turning the camera. The touch helper now reports how fast the player's fingers move (speeds only, never positions), and while the finger on the same half of the screen as the extra finger's resting place is turning the camera, the controller closes the gap at 40% of its strength and takes it back as the finger stops (it falls away over about a third of a second). On an emulator, a swipe injected through the touchscreen device was noticed 0.4 s after it began; a swipe on the other half of the screen and a finger resting on the look area were not noticed at all.
- A tap on an open list of the in-game menu, such as the language list, closed the menu instead of choosing when it landed past the menu's edge. The list takes the tap now.
- On a phone whose detector runs slower than about ten times a second, the box on a target dimmed between two results, and the controller let go of the target and the finger touched down again. The simulation put numbers on it: at 8 detections a second no box was drawn on the target for 41% of the time and the finger touched down 8 times in 10 seconds; at 5 a second, 78% and 5 times. A result that has been waited for is now taken as the newest word there is, and a target that one result missed is held, and forgotten, by how often results come. The same runs now draw a box all the time and touch down once.
- A volume key device that went away (a Bluetooth headset out of range) made the touch helper use nearly a whole processor core for as long as it ran: 182 ticks of the emulator's processor in 2 seconds, where it needs none. The helper lets the device go and says so in its log.
- The first finger put down after the touch helper took the screen over could be filed in slot 0 of the virtual touchscreen when the real one had put it in another slot, because the touchscreen does not name a slot that has not changed. A finger the emulator's touchscreen put in slot 1 showed up in slot 0 before the fix and in slot 1 after it. The helper now starts from the slot the touchscreen is at.
- The extra finger could be lifted late, or put down in the wrong order, when the touchscreen had lost events: commands that waited for the end of a group of events went out after newer ones. They go out in order once the screen has caught up.
- A touchscreen that stopped answering while the helper caught up with lost events could leave the helper reading a freed merger. It stops reading at once now.
- The touch helper says in its log when no volume key can be read, which means holding both keys will not give the touchscreen back, and it closes the key devices it could not use.
- Turning the screen left the controller remembering where it had given up on a box, and where the targets and the camera's picture had been, in the old orientation. All three are forgotten.
- A setting changed while another was being saved by AimBuddy (the learned colour, the camera it measured) could be put back to what it was, because each part took a copy of all the settings and handed it back. They change settings under the lock now, and a setting changed before the file was read is no longer lost to it.
- The detector took the GPU for being faster even if it gave a wrong answer, which some graphics drivers do (zeros, or numbers that overflowed half precision), and then saw nobody. It runs a test picture through both and uses the GPU only if its answer is the CPU's.
- A pair of model files that was imported became the model in use whatever it held, and Start then failed. The pair is loaded and run once when it is imported, and one the detector cannot read is deleted with a message, so the model in use stays as it was. Checked on the emulator with a pair of damaged files, which were refused and left nothing behind, and with a good pair, which was kept.

### Upgrade note
It installs over 0.3.5-beta.1 and keeps the settings and the downloaded model. A copy of 0.3.5-beta.1 can also take it through the app's own updater, under Settings, About, Updates.

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
