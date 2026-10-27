@echo off
setlocal
cd /d "%~dp0.."

echo ==================================================
echo [08] Auto-label raw_frames using a teacher detector
echo ==================================================
echo.
echo This script runs a large COCO-pretrained YOLO over every
echo image in raw_frames/ and writes YOLO-format labels into
echo dataset/train/. The teacher is yolov8x.pt by default
echo (auto-downloads on first run, ~135MB).
echo.
echo Spot-check the labels before training. The teacher catches
echo 95%% of humans but occasionally boxes vehicles, posters,
echo and NPCs. Delete those by hand or via Roboflow.
echo.

if not exist ".venv\Scripts\python.exe" (
  echo [setup] Virtual environment not found. Running scripts\01_setup_environment.bat...
  call scripts\01_setup_environment.bat
  if errorlevel 1 (
    echo ERROR: Setup failed.
    exit /b 1
  )
)

call .venv\Scripts\activate.bat
if errorlevel 1 (
  echo ERROR: Failed to activate virtual environment.
  exit /b 1
)

REM Defaults (the teacher is yolov8x.pt at imgsz 1280, conf 0.30); override with extra args, e.g.:
REM   scripts\08_auto_label.bat --teacher yolo11x.pt --conf 0.25 --imgsz 1280
REM   scripts\08_auto_label.bat --teacher-onnx "C:\path\to\Your Game.onnx" --conf 0.40 --write-negatives
python src\auto_label.py ^
    --in raw_frames ^
    --out dataset\train ^
    %*

set RC=%ERRORLEVEL%

if %RC% EQU 0 (
  echo.
  echo Auto-labeling complete.
  echo - Images: training\dataset\train\images
  echo - Labels: training\dataset\train\labels
  echo.
  echo Next:
  echo   1. Spot-check labels  ^(Roboflow / labelImg^)
  echo   2. Optional: scripts\09_mine_negatives.bat ^(menus, vehicles^)
  echo   3. scripts\03_validate_dataset.bat
  echo   4. scripts\04_train_adaptive.bat
) else (
  echo.
  echo Auto-labeling failed with code %RC%.
)

exit /b %RC%
