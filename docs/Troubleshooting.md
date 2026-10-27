# Troubleshooting

When something does not work: first what the app says, then the build, the touch access, detection, the aim, the models and the training pipeline.

## Starting

The Home screen says what is wrong in a sentence. What each one means:

| Message | Cause and fix |
|---------|---------------|
| Display over other apps is not allowed. | Open Settings, find AimBuddy in the list of apps that may display over other apps, and turn it on. Then press Start. |
| Screen capture was not allowed. | Press Start again and accept the system's screen capture question. The assist needs to see the whole screen to see the game. |
| Screen capture ended. | The system stopped the screen capture. Press Start again. |
| The detection model could not be loaded. | The chosen model's files are missing or are not a valid NCNN model. Download AimBuddy General again under Models, or import the model again. |
| Get a model first. | The app carries no model: the first one comes from the Store tab of the Models page (a small download) or from files you import. Press Start again once it is there. |
| AimBuddy could not start. | Something unexpected failed. Run `adb logcat -s AssistService:W AimBuddy_Native:E` and look for the first error. |

If AimBuddy stops by itself during a game, the phone's battery manager probably ended it. Exclude AimBuddy from battery optimization, see the table in [Shizuku setup](ShizukuSetup.md).

## Touch access

Without it AimBuddy still outlines enemies, but the assist cannot move your aim. See [Shizuku setup](ShizukuSetup.md) for starting Shizuku. For root, grant it when your root manager asks.

| What the Home screen says | Cause and fix |
|---------------------------|---------------|
| Aim assist needs Shizuku or root. | Neither is available. Start Shizuku, or grant root, then press Stop and Start. |
| This phone does not let the touch helper use the touchscreen. | Shizuku or root started the helper, but this phone refused it access to the touchscreen. Under Settings, Touch, try the other choice: root if you were using Shizuku, or Shizuku if you were using root. |
| Another program is holding the touchscreen. | Another app that reads the touchscreen directly is running. Close it. |
| The touch helper could not be started. | Press Stop and Start again. If it keeps happening, check `adb logcat -s TouchHelper:W aimtouch:V` for the reason. |

Other things you may notice:

- **The touchscreen does not answer for a moment after AimBuddy starts, or after the screen turns.** That is on purpose. The system ignores a touch that begins right after the touchscreen changes hands, so the assist waits 700 ms.
- **The touchscreen does not answer at all.** Hold both volume keys: the helper gives the touchscreen back and stops. Or press Stop in the notification. If the helper is killed, the system gives the touchscreen back by itself.
- **The assist does nothing although touch access is ready.** The assist starts switched off every time. Open the in-game menu, go to the Aim tab and switch it on.

## Detection

- **No boxes at all.** If streamer mode is on, nothing is drawn over the game, so turn it off to see them. Otherwise lower the detection confidence, and check that the enemies are inside the detection area (the blue square, which shows while the menu is open and, with Detection area outline on, while you play).
- **Boxes on things that are not enemies.** Raise the detection confidence. If it keeps happening in your game, train a model on it, see [Training](Training.md).
- **Boxes arrive late or only a few times a second.** See [Performance](Performance.md), which also explains the numbers of the performance readout. When the phone reports that it is hot, AimBuddy runs fewer detections a second on purpose, and goes back to full speed once the phone has cooled down.

## Aim

| Symptom | What to do |
|---------|------------|
| The aim overshoots, swings or crawls | Give it a minute: with Calibrate automatically on, the first moves are small on purpose while it measures how your game reacts. If it still misbehaves, turn calibration off and set Camera sensitivity and Response delay by hand. |
| The aim goes up when the target is below | Switch on Invert vertical. |
| The assist's finger keeps lifting and landing again | The game does not answer it. The touch area is probably on something that does not turn the view, such as a button. Move it in the in-game menu to where a finger would turn the camera, away from your own thumbs. |
| The assist turns towards my own weapon or character | The detector takes it for an enemy. A box that stays put on the screen while the camera turns past it can never be reached, so after about half a second the assist gives up on it for a few seconds, longer each time. A model trained on your game stops it for good, see [Training](Training.md). |
| It jumps between two targets | Choose another Target pick. The assist already keeps its target until another is clearly better. |
| It aims at the wrong part | Change Aim at. A model that detects heads is aimed at exactly; for one that does not, the point is worked out from the shape of the box. |

## The overlay

- **The floating button is gone.** In streamer mode it is invisible but still there: tap where it was, or use the Menu button in the notification.
- **A recording shows black while the menu is open.** That is on purpose in streamer mode, so that the menu never reaches the recording. It is a few moments.
- **The menu does not open or close.** Hold the button to stop AimBuddy, then start it again.

## Models and the store

| Message | Cause and fix |
|---------|---------------|
| Can't reach the store. | The list is one small file on GitHub. Check the connection and try again. Without a connection you can still import a model from files. |
| That model could not be downloaded. | The connection dropped or a file did not match its checksum. The model you had is untouched. Try again. |
| That model could not be imported. | Choose the `.param` file first and then the matching `.bin` file of the same export. |
| Can't reach GitHub. (an update) | Check the connection and try again. |
| The download could not be verified. (an update) | The file does not match what GitHub lists for it, is not a newer build of AimBuddy, or is signed with another key than the installed copy, for example when a debug build is installed. Try again. If the installed copy is a debug build, remove it and install the release by hand, see [Releasing](Releasing.md). The settings go with it. |
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

See "Installing a build by hand" in [Releasing](Releasing.md).

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
