# AimBuddy Training Folder Guide

This folder contains the complete data, training, and NCNN export workflow used by AimBuddy.

The model contract is fixed to `yolo26n.pt` with single-class detection (`enemy`, class id `0`).

## Required Environment

- Python 3.10 to 3.12
- Windows 10 or 11 (64-bit)
- Installed dependencies from `requirements.txt`

Recommended hardware:

- CPU: 8+ cores
- RAM: 16 GB+
- Disk: 30 GB+ free
- GPU: NVIDIA with CUDA 12.1

Minimum supported hardware:

- CPU: 4 cores
- RAM: 8 GB
- Disk: 15 GB free
- GPU: optional

## Folder Structure

```text
training/
  config/
  dataset/
  outputs/
    reports/
    runs/
    export/
  raw_frames/
  scripts/
    01_setup_environment.bat
    02_extract_frames.bat
    03_validate_dataset.bat
    04_train_adaptive.bat
    05_train_manual.bat
    06_export_ncnn.bat
    07_run_full_pipeline.bat
  src/
  videos/
  requirements.txt
  yolo26n.pt
```

Python entrypoints auto-create missing required folders, including output and deployment targets.

## Script Order

Run in this order when executing manually:

1. `scripts\01_setup_environment.bat` (creates `.venv`, installs deps, and ensures `yolo26n.pt`)
2. `scripts\02_extract_frames.bat` (optional if starting from videos)
3. `scripts\03_validate_dataset.bat`
4. `scripts\04_train_adaptive.bat` or `scripts\05_train_manual.bat`
5. `scripts\06_export_ncnn.bat`

One-command path:

```powershell
cd training
scripts\07_run_full_pipeline.bat
```

Pipeline flags (passed to `src/run_pipeline.py`):

- `--manual` or `--adaptive`
- `--skip-export`
- `--non-strict-preflight`
- `--config <path>`

Example:

```powershell
scripts\07_run_full_pipeline.bat --manual --skip-export
```

Preflight strictness behavior:

- `01_setup_environment.bat` uses non-strict preflight (`--non-strict`).
- `04_train_adaptive.bat` and `05_train_manual.bat` use strict preflight.
- `07_run_full_pipeline.bat` is strict by default, optional non-strict via `--non-strict-preflight`.

## Configuration

Config file: `training/config/config.ini`

- `[paths]`: model, dataset, outputs, deployment paths
- `[training]`: manual hyperparameters
- `[adaptive]`: dataset-size adaptive training rules
- `[export]`: NCNN export options

Adaptive dataset thresholds resolved in `src/training_config.py`:

- Small (`<300` train images): epochs around 260, batch constrained to 8-12
- Medium (`300 to <1200`): epochs around 180, batch constrained to 12-20
- Large (`>=1200`): epochs around 120, batch constrained to 16-32

Resolved values are written to `training/outputs/reports/selected_training_config.json`.

## Dataset Requirements

Required YOLO label format:

```text
class_id x_center y_center width height
```

Rules:

- values normalized to `[0,1]`
- class id must be `0`
- no cross-split leakage between train, valid, and test

Expected dataset paths:

- `training/dataset/data.yaml`
- `training/dataset/train/images`
- `training/dataset/train/labels`
- `training/dataset/valid/images`
- `training/dataset/valid/labels`
- `training/dataset/test/images`
- `training/dataset/test/labels`

## Output Paths

- Reports: `training/outputs/reports`
- Weights: `training/outputs/runs/detect/train/weights`
- Exported NCNN artifacts (working copy): `training/outputs/export`
- Deployment target for app runtime: `app/src/main/assets/models`

The export script writes both locations and keeps filenames aligned with runtime constants:

- `models/yolo26n-opt.param`
- `models/yolo26n-opt.bin`

## Preflight and Error Reports

Preflight verifies Python version, CPU cores, RAM, disk space, and CUDA availability.

Review these files after each run:

- `training/outputs/reports/preflight_report.json`
- `training/outputs/reports/dataset_report.json`
- `training/outputs/reports/selected_training_config.json`
- `training/outputs/reports/pipeline_last_run.json`
- `training/outputs/reports/pipeline_last_run.log`

Model contract validation before export:

```powershell
cd training
python src\check_model_contract.py --weights outputs\runs\detect\train\weights\best.pt
```

## Troubleshooting

- NVIDIA GPU detected but no CUDA in torch: reinstall CUDA-enabled torch and verify Python 3.10 to 3.12.
- Validation errors: fix dataset labels before retraining.
- Export completes but runtime fails to load model: verify files in `app/src/main/assets/models`.

For end-to-end training context, see [docs/Training.md](../docs/Training.md).
