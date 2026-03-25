# Troubleshooting

This guide covers common setup, build, runtime, and training issues for AimBuddy.

## Android Build Issues

### NDK or CMake mismatch

Symptoms:

- Gradle native build fails
- CMake configuration errors

Checks:

1. Confirm Android NDK is `29.0.13113456 rc1`.
2. Confirm CMake is `3.22.1`.
3. Re-run:

```powershell
./gradlew.bat clean assembleDebug
```

### NCNN library not found

Symptoms:

- Native link error for NCNN

Checks:

1. Verify NCNN artifacts are present under `app/src/main/cpp/ncnn/` as expected by CMake.
2. Rebuild after syncing native dependencies.

## Installation and Runtime Issues

### App installs but overlay does not appear

Checks:

1. Ensure overlay permission is granted.
2. Ensure MediaProjection permission is granted after tapping Start.
3. Verify screen recording prompt was accepted for the current app session.

### Capture service starts but no detections are shown

Checks:

1. Verify model files are present in `app/src/main/assets/models`.
2. Confirm model export completed without errors.
3. Confirm confidence threshold is not set too high in settings.

### Root unavailable

Expected behavior:

- Visual Assist Mode should still work.
- Assisted input mode remains disabled.

Checks:

1. Confirm root manager is properly installed and `su` works.
2. Reopen app and allow root if prompted.
3. If still unavailable, continue in visual mode.

## Training Pipeline Issues

### Environment setup fails

Checks:

1. Use Python 3.10 to 3.12.
2. Re-run setup:

```powershell
cd training
scripts\01_setup_environment.bat
```

### Preflight fails

Review:

- `training/outputs/reports/preflight_report.json`

Common causes:

- Python version out of supported range
- RAM or disk below minimum
- Missing required packages

### CUDA not available in torch

Symptoms:

- NVIDIA GPU detected but training runs CPU-only

Checks:

1. Confirm Python version is 3.10 to 3.12.
2. Reinstall CUDA-enabled torch wheel.
3. Re-run preflight and confirm GPU status in report.

### Dataset validation fails

Review:

- `training/outputs/reports/dataset_report.json`

Fixes:

1. Correct malformed YOLO label rows.
2. Ensure normalized coordinate ranges.
3. Remove split leakage between train, valid, and test.

### Pipeline fails at train or export stage

Review:

- `training/outputs/reports/pipeline_last_run.json`
- `training/outputs/reports/pipeline_last_run.log`

These files include failing step name and traceback details.

## Useful Commands

```powershell
./gradlew.bat clean assembleDebug
cd training
scripts\07_run_full_pipeline.bat
```

## Related Docs

- [README](../README.md)
- [Architecture](Architecture.md)
- [Settings Guide](SettingsGuide.md)
- [Training Guide](Training.md)
- [Performance Guide](Performance.md)
