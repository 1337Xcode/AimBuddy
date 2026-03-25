# AimBuddy
AimBuddy is an AI-based Android Aim Assistant for real-time screen capture, object detection, target tracking, visual guidance overlays, and optional assisted input.

## Project in Brief
- Platform: Android (arm64-v8a)
- App stack: Kotlin + Jetpack Compose + native C++
- Inference stack: NCNN runtime with `yolo26n` training and export pipeline
- Modes:
  - Visual Assist Mode (ESP): no root required
  - Assisted Input Mode (Aim Assist): root required for input injection

## Runtime Modes
- **Visual Assist Mode** (no root): capture, inference, tracking, and on-screen overlay guidance.
- **Assisted Input Mode** (root required): visual assist features plus low-latency input injection.

If root is unavailable, the app remains functional in Visual Assist Mode.

## Supported Android Devices
### Runtime and Usage Specs
| Item | Minimum | Recommended |
| --- | --- | --- |
| Android version | Android 11 (API 30) | Android 13+ |
| CPU ABI | arm64-v8a | Newer arm64 flagship or upper mid-range |
| Graphics | OpenGL ES 3.1 support | OpenGL ES 3.2 or Vulkan-capable GPU |
| RAM | 6 GB | 8 GB or higher |
| Free storage | 2 GB | 5 GB or higher |
| Root access | Not required for visual mode | Required only for assisted input mode |

### Training and Export Environment Specs
| Item | Minimum | Recommended |
| --- | --- | --- |
| OS | Windows 10 or 11 (64-bit) | Windows 11 (64-bit) |
| Python | 3.10 to 3.12 | 3.11 |
| CPU | 4 cores | 8 or more cores |
| RAM | 8 GB | 16 GB or higher |
| Free storage | 15 GB | 30 GB or higher |
| GPU | CPU-only supported | NVIDIA GPU with CUDA 12.1 |

## Quick Start
### 1) Build and Install
```powershell
./gradlew.bat clean assembleDebug
./gradlew.bat installDebug
```
See [Build and Install Details](#build-and-install-details) for release builds, manual APK install, and prerequisites.

### 2) First Launch
1. Open the app on device.
2. Grant overlay permission.
3. Grant MediaProjection screen capture permission.
4. Start runtime processing from the UI.
5. If root is present, assisted input features become available.

### 3) Train and Export Model (Optional)
```powershell
cd training
scripts\07_run_full_pipeline.bat
```
See [Train and Export Model](#train-and-export-model) for outputs and individual scripts.

## Build and Install Details
### Prerequisites
- Android SDK 35
- Android NDK 29.0.13113456 rc1
- CMake 3.22.1
- Java 11+
- Android platform tools (`adb`) for manual install checks

### Compile
```powershell
./gradlew.bat clean assembleDebug
```
Release build:
```powershell
./gradlew.bat clean assembleRelease
```

### Install
Preferred:
```powershell
./gradlew.bat installDebug
```
Manual alternative:
```powershell
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

## Train and Export Model
From the repository root:
```powershell
cd training
scripts\07_run_full_pipeline.bat
```
This runs preflight, dataset validation, training, and NCNN export.

Key outputs:
- Reports: `training/outputs/reports`
- Weights: `training/outputs/runs/detect/train/weights`
- NCNN export: `training/outputs/export`
- Deployment target: `app/src/main/assets/models`

Use these individual scripts when needed:
```powershell
scripts\01_setup_environment.bat
scripts\03_validate_dataset.bat
scripts\04_train_adaptive.bat
scripts\05_train_manual.bat
scripts\06_export_ncnn.bat
```

## External Credits
AimBuddy builds on the following open-source tools and libraries.

### Android App and Runtime
- [AndroidX Core/AppCompat](https://developer.android.com/jetpack/androidx)
- [Material Components for Android](https://github.com/material-components/material-components-android)
- [Jetpack Compose](https://developer.android.com/jetpack/compose)
- [AndroidSVG](https://bigbadaboom.github.io/androidsvg/)
- [NCNN](https://github.com/Tencent/ncnn)
- [Dear ImGui](https://github.com/ocornut/imgui)

### Training and Export Stack
- [Ultralytics](https://github.com/ultralytics/ultralytics)
- [PyTorch](https://pytorch.org/)
- [TorchVision](https://pytorch.org/vision/stable/index.html)
- [OpenCV](https://opencv.org/)
- [NumPy](https://numpy.org/)
- [ONNX](https://onnx.ai/)
- [ONNX Runtime](https://onnxruntime.ai/)

See `training/requirements.txt` and `app/build.gradle` for exact dependency declarations and versions.

## Documentation Map
- [Architecture](docs/Architecture.md)
- [Settings Guide](docs/SettingsGuide.md)
- [Training Guide](docs/Training.md)
- [Performance Guide](docs/Performance.md)
- [Troubleshooting](docs/Troubleshooting.md)
- [Contributing](CONTRIBUTING.md)

## Repository Layout
- `app/`: Android app and native runtime
- `training/`: scripts, Python pipeline, and export tooling
- `docs/`: technical and operational documentation
- `archive_local/`: local historical notes and drafts

## License
This project is released under the AimBuddy Community Free Use License v1.0.

Key terms:
- free use, modification, and redistribution are allowed
- selling or commercial monetization is not allowed
- derivative works must remain free and use the same license
- attribution to the original project is required
- software is provided as-is with no warranty and no liability

See [LICENSE](LICENSE) for full terms.

## Legal and Usage Notice
Use this project only in authorized environments and only where local law, platform policy, and software terms allow such testing.
