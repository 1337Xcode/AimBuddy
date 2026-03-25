# Training Guide

This document defines the supported training and NCNN export workflow for AimBuddy.

The model contract is based on `yolo26n.pt` with a single class (`enemy`, class id `0`).

## Environment Requirements

- Python 3.10 to 3.12
- Dependencies from `training/requirements.txt`
- Optional NVIDIA GPU for faster training

Practical hardware guidance:

| Item | Minimum | Recommended |
| --- | --- | --- |
| CPU | 4 cores | 8 or more cores |
| RAM | 8 GB | 16 GB or higher |
| Free disk | 15 GB | 30 GB or higher |
| GPU | CPU-only supported | NVIDIA GPU with CUDA 12.1 |

## Standard Pipeline

1. Setup environment and dependencies.
2. Validate machine and dataset preflight conditions.
3. Train model (adaptive or manual mode).
4. Export trained weights to NCNN.
5. Deploy exported artifacts to Android app assets.

One-command path:

```powershell
cd training
scripts\07_run_full_pipeline.bat
```

## Script Reference

Run from `training/`:

```powershell
scripts\01_setup_environment.bat
scripts\02_extract_frames.bat
scripts\03_validate_dataset.bat
scripts\04_train_adaptive.bat
scripts\05_train_manual.bat
scripts\06_export_ncnn.bat
scripts\07_run_full_pipeline.bat
```

## Dataset Contract

Required structure:

- `training/dataset/data.yaml`
- `training/dataset/train/images` and `training/dataset/train/labels`
- `training/dataset/valid/images` and `training/dataset/valid/labels`
- `training/dataset/test/images` and `training/dataset/test/labels`

YOLO label row format:

```text
class_id x_center y_center width height
```

Requirements:

- all coordinates normalized to `[0,1]`
- class id must be `0`
- no data leakage across train, valid, and test
- include background samples to reduce false positives

## Adaptive and Manual Training

- Adaptive mode: auto-selects safer values by dataset size.
- Manual mode: uses explicit values from `training/config/config.ini`.

Commands:

```powershell
scripts\04_train_adaptive.bat
scripts\05_train_manual.bat
```

Resolved configuration is written to:

- `training/outputs/reports/selected_training_config.json`

## Output and Deployment Contract

Generated outputs:

- reports: `training/outputs/reports`
- weights: `training/outputs/runs/detect/train/weights`
- NCNN export: `training/outputs/export`

Deployment target for app runtime:

- `app/src/main/assets/models`

Exported NCNN files must match runtime loader expectations in native detector code.

## Preflight and Reporting

Preflight validates:

- Python version compatibility (3.10 to 3.12)
- CPU core count
- memory and disk thresholds
- CUDA availability (warning if unavailable)

Report files:

- `training/outputs/reports/preflight_report.json`
- `training/outputs/reports/dataset_report.json`
- `training/outputs/reports/pipeline_last_run.json`
- `training/outputs/reports/pipeline_last_run.log`

## Troubleshooting Quick Notes

- If NVIDIA GPU is detected but CUDA is unavailable in torch, confirm Python 3.10 to 3.12 and reinstall CUDA-enabled torch wheel.
- If dataset validation fails, fix labels first before increasing dataset size.
- If export succeeds but app cannot infer, verify files are present in `app/src/main/assets/models`.

For script-level folder details, see [training/README.md](../training/README.md).
