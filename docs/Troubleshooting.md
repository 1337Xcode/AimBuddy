# Troubleshooting

When something does not work: first installing the app, then what the app says, the touch access, detection, the aim, the models, the build and the training pipeline.

## Installing

| What Android says | Cause and fix |
|-------------------|---------------|
| App not installed as package conflicts with an existing package. | The copy on your phone is signed with another key than the file you are installing, and Android never lets one replace the other. Releases from 0.3.0-beta.2 on share one key, so they install over each other. The usual causes are a 0.3.0-beta.1 that you signed yourself, or a debug build made on a computer, which carries that computer's own key. Uninstall AimBuddy once, then install again. The settings and downloaded models go with it. |
| App not installed. (and nothing more) | The file is damaged or incomplete, or it is for another kind of phone: AimBuddy needs an arm64 phone with Android 11 or newer. Download the file again. |
| A second AimBuddy appears after the install. | That is the debug build (`com.aimbuddy.debug`), which installs beside the app instead of replacing it. Its name on the phone is AimBuddy Debug. Keep the app and delete the debug build if you do not need it. |

## Starting

The Home screen says what is wrong in a sentence. What each one means:

| Message | Cause and fix |
|---------|---------------|
| Display over other apps is not allowed. | Open Settings, find AimBuddy in the list of apps that may display over other apps, and turn it on. Then press Start. |
| Screen capture was not allowed. | Press Start again and accept the system's screen capture question. The assist needs to see the whole screen to see the game. |
| Screen capture ended. | The system stopped the screen capture. Android lets one app capture the screen at a time, so starting a screen recording or a cast stops AimBuddy, and so does locking the screen or stopping the sharing from the status bar. Press Start again once the recording is over. |
| The detection model could not be loaded. | The chosen model's files are missing or are not a valid NCNN model. Download AimBuddy General again under Models, or import the model again. |
| Get a model first. | The app carries no model: the first one comes from the Store tab of the Models page (a small download) or from files you import. Press Start again once it is there. |
| AimBuddy could not start. | Something unexpected failed. Run `adb logcat -s AssistService:W AimBuddy_Native:E` and look for the first error. |

If AimBuddy stops by itself during a game, the phone's battery manager probably ended it. Exclude AimBuddy from battery optimization, see the table in [Shizuku setup](ShizukuSetup.md).

## Touch access

Without it AimBuddy still outlines enemies, but the assist cannot move your aim. See [Shizuku setup](ShizukuSetup.md) for starting Shizuku. For root, grant it when your root manager asks.

The Touch access screen (the Touch access row on Home, or Settings, Touch) shows where each route stands and has a Test touch access button. The test starts the touch helper the way Start does, says whether it took the touchscreen over, and stops it again. It cannot run while the assist is running, and a press of Start while it runs is refused with a message: press it again once the test has finished.

| Shizuku, on the Touch access screen | What to do |
|-------------------------------------|------------|
| Ready | Nothing. It is running and has allowed AimBuddy. |
| Tap to allow | It is running but has not allowed AimBuddy. Tap the row and answer Shizuku's question. If it does not ask again, the row opens Shizuku: allow AimBuddy in its list of apps. |
| Not running | It is installed but stopped. Tap the row to open Shizuku and start it, see [Shizuku setup](ShizukuSetup.md). |
| Not installed | The row opens the setup guide. |

Root is shown as Not asked yet until you press Start or Test, because the only way to find out is to ask, and the root manager may show a prompt. After a refusal, or a prompt left unanswered, it says Not available.

| What the test says | Cause and fix |
|--------------------|---------------|
| Works through Shizuku, or through root. | The helper started and took the touchscreen over. |
| No route was available. | Neither route could start the helper: Shizuku has to be running and allow AimBuddy, or root has to be granted. If you chose one route, the message names it (Shizuku is not available, or Root was not granted). |
| This phone does not let the touch helper use the touchscreen, or Another program is holding the touchscreen, or The touch helper could not be started. | The same causes as the table below. Under the message the test shows what the helper printed. |

| What the Home screen says | Cause and fix |
|---------------------------|---------------|
| Aim assist needs Shizuku or root. | Neither is available. Start Shizuku, or grant root, then press Stop and Start. |
| This phone does not let the touch helper use the touchscreen. | Shizuku or root started the helper, but this phone refused it access to the touchscreen. On the Touch access screen, try the other choice: root if you were using Shizuku, or Shizuku if you were using root, and press Test touch access. |
| Another program is holding the touchscreen. | Another app that reads the touchscreen directly is running. Close it. |
| The touch helper could not be started. | Press Stop and Start again. If it keeps happening, check `adb logcat -s TouchHelper:W aimtouch:V` for the reason. |

Other things you may notice:

- **The touchscreen does not answer for a moment after AimBuddy starts, or after the screen turns.** That is on purpose. The system ignores a touch that begins right after the touchscreen changes hands, so the assist waits 700 ms.
- **The touchscreen does not answer at all.** Hold both volume keys: the helper gives the touchscreen back and stops. Or press Stop in the notification. If the helper is killed, the system gives the touchscreen back by itself.
- **The assist does nothing although touch access is ready.** The assist starts switched off every time. Open the in-game menu, go to the Aim tab and switch it on.

## Detection

- **No boxes at all.** If streamer mode is on, nothing is drawn over the game, so turn it off to see them. Otherwise lower the detection confidence, and check that the enemies are inside the detection area (the blue square, which shows while the menu is open and, with Detection area outline on, while you play).
- **Boxes on things that are not enemies.** Raise the detection confidence. If it keeps happening in your game, train a model on it, see [Training](Training.md).
- **Colour detection finds nothing.** The choice is taken when you press Start, so press Start again after choosing Colour. Then open the in-game menu, press Learn colour and tap the outline itself, not the player's body. A dull or grey spot cannot be learned, and the bar says so. If the outline's shade changes with the light, raise the colour tolerance; if the players are far away, lower the smallest target.
- **Colour detection boxes things that are not enemies.** The same colour is in the scenery or on the game's buttons. Learn a colour the game uses for nothing but enemies, lower the colour tolerance, or raise the smallest target.
- **Boxes arrive late or only a few times a second.** See [Performance](Performance.md), which also explains the numbers of the performance readout. When the phone reports that it is hot, AimBuddy runs fewer detections a second on purpose, and goes back to full speed once the phone has cooled down.

## Aim

| Symptom | What to do |
|---------|------------|
| The aim overshoots, swings or crawls | Give it a minute: with Calibrate automatically on, the first moves are small on purpose while it measures how your game reacts. If it still misbehaves, turn calibration off and set Camera sensitivity and Response delay by hand. |
| The aim goes up when the target is below | Switch on Invert vertical. |
| The assist's finger keeps lifting and landing again | The game does not answer it. The touch area is probably on something that does not turn the view, such as a button. Move it in the in-game menu to where a finger would turn the camera, away from your own thumbs. |
| The assist turns towards my own weapon or character | The detector takes it for an enemy. A box that stays put on the screen while the camera turns past it can never be reached, so after about half a second the assist gives up on it for a few seconds, longer each time, and the box is no longer drawn. A model trained on your game stops it for good, see [Training](Training.md). |
| The gun fires by itself | The touch area is over the fire button, or another button, in your game's layout, and the extra finger presses it whenever it touches down. Move the touch area to an empty part of the screen that you swipe to look around: in-game menu, second tab, Touch area, Move or resize on screen. |
| It jumps between two targets | Choose another Pick target by, or Sticky under Switching targets. The assist already keeps its target until another is clearly better. The model cannot tell teammates from enemies yet, so a teammate near the crosshair can take the target. |
| It aims at the wrong part | Change Aim at. A model that detects heads is aimed at exactly; for one that does not, the point is worked out from the shape of the box. |

## The overlay

- **The floating button is gone.** In streamer mode it is invisible but still there: tap where it was, or use the Menu button in the notification.
- **A recording shows a black panel while the menu is open.** That is on purpose in streamer mode, so that the menu never reaches the recording. The panel is as tall as the screen and as wide as the menu; the game shows everywhere else. A phone that does not allow a secure window keeps the menu shut in streamer mode and says so; turn streamer mode off to open it.
- **Move or resize on screen and Learn colour are dimmed.** Both draw over the game, which streamer mode does not do. Turn streamer mode off, set them, and turn it back on.
- **The menu does not open or close.** Hold the button to stop AimBuddy, then start it again.

## Models and the store

| Message | Cause and fix |
|---------|---------------|
| Can't reach the store. | The list is one small file on GitHub. Check the connection and try again. Without a connection you can still import a model from files. |
| No connection. The list below was saved earlier. | The store could not be reached now, so the list from the last visit is shown. Models on it can still be downloaded if the connection is back; Try again reads the list afresh. |
| No connection. Check the internet and try again. (under a model) | The phone could not reach the store, or the connection broke on the way. Tap Try again once it is back. |
| The download was damaged. Try again. (under a model) | What arrived was not the size or the checksum the store lists. The model you had is untouched. Tap Try again. |
| Not enough space on the phone. (under a model) | Free some storage and tap Try again. |
| That model could not be downloaded. | Anything else, such as the store answering with an error. The model you had is untouched. Try again. |
| That model could not be imported. | The files could not be copied. Choose the `.param` file first and then the matching `.bin` file of the same export. |
| AimBuddy cannot read that pair of files as a detection model. | The files were copied but the detector could not load them and run them once, so they are not kept and the model in use stays as it was. Check that the `.param` and the `.bin` come from the same export, and that the model is a YOLO detector with the output shape the app reads (see [Training](Training.md)). |

A download shows how much has arrived, how fast and how long is left, with a Cancel button beside it. Cancelling ends the read at once and leaves nothing behind.
| Can't reach GitHub. (an update) | Check the connection and try again. |
| The download could not be verified. (an update) | The file does not match what GitHub lists for it, is not a newer build of AimBuddy, or is not signed at all. Try again. |
| The update is signed with another key. (an update) | Android never replaces an app with one signed by another key. The installed copy was probably built on another computer, or signed by you. Tap Releases, uninstall AimBuddy once, and install the release from there by hand. The settings and downloaded models go with it. |
| The update could not be installed. | The system's installer refused the update. Free some storage and try again; if it repeats, install the release by hand. |
| Allow AimBuddy to install apps, then come back. | Open Settings, Apps, Special app access, Install unknown apps, choose AimBuddy and allow it. |

## Settings

- Settings are kept in `settings.cfg` in AimBuddy's private storage. A change is saved a moment after you make it and again when the app goes out of sight.
- The assist's on/off switch is not saved, on purpose.
- Settings, About, Reset all settings puts everything back to how it started and keeps the language.

## Building

### NDK or CMake does not match

Gradle's native build fails with CMake errors or missing symbols. Install Android NDK 29.0.13113456 and CMake 3.22.1 with the SDK Manager, then rebuild clean:

```sh
./gradlew clean assembleDebug
```

### NCNN is not found

Native linker errors that name NCNN symbols mean the prebuilt libraries under `app/src/main/cpp/ncnn/` are missing or are for another ABI. AimBuddy builds for `arm64-v8a` only.

### `__libcpp_verbose_abort` is undefined

The NCNN libraries were built with a different libc++ than the NDK's. `esp_jni.cpp` defines the missing symbol; if you see this after updating NCNN, check that definition is still there.

### The installer says the package is invalid or conflicts with an existing package

See [Installing](#installing) above, and "Installing a build by hand" in [Releasing](Releasing.md) for the commands that remove a copy that will not go.

## Training

### The environment does not set up

1. Use Python 3.10 to 3.12; 3.11 is recommended.
2. Run the setup again, then read `training/outputs/reports/preflight_report.json`:

   ```powershell
   cd training
   scripts\01_setup_environment.bat
   ```

3. If strict preflight blocks scripts `04` or `05`, run the whole pipeline with the relaxed check:

   ```powershell
   scripts\07_run_full_pipeline.bat --non-strict-preflight
   ```

### CUDA is not available in torch

An NVIDIA GPU is found but training runs on the CPU. Use Python 3.10 to 3.12, install CUDA 12.1 and the matching torch wheel, and check with `python -c "import torch; print(torch.cuda.is_available())"`.

### The dataset does not validate

Read `training/outputs/reports/dataset_report.json`. The usual causes are label rows that are not `class_id x_center y_center width height`, coordinates outside 0 to 1, the same image in two splits, and no background-only images.

### The export works but the app does not detect

1. The pipeline writes `model.param`, `model.bin` and `model.json` to `training/outputs/export/` and does not touch the app. Import the two NCNN files under Models, Import.
2. If Start says the detection model could not be loaded, the two files are not from the same export.
3. Lower the detection confidence: a model that was trained briefly gives low scores.

### The model contract check fails

`check_model_contract.py` reports a class count or shape that does not match. Check that `training/dataset/data.yaml` has exactly one class (`nc: 1`), then retrain and check again:

```powershell
cd training
python src\check_model_contract.py --weights outputs\runs\detect\train\weights\best.pt
```

## Useful commands

```sh
./gradlew assembleDebug                                   # build
./gradlew testDebugUnitTest                               # Kotlin tests
sh app/src/test/native/run.sh                             # C++ core tests
adb install -r app/build/outputs/apk/debug/app-debug.apk  # install a debug build
adb logcat -s AimBuddy_Native:I                           # what the native code does (debug builds)
adb logcat -s AimBuddy_Native:E                           # errors only (release builds)
```

## Related

- [README](../README.md)
- [Architecture](Architecture.md)
- [Settings guide](SettingsGuide.md)
- [Performance](Performance.md)
- [Shizuku setup](ShizukuSetup.md)
- [Training](Training.md)
