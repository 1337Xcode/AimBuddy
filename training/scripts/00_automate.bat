@echo off
setlocal
cd /d "%~dp0.."

echo ==================================================
echo [00] One-button automation: videos -^> NCNN model
echo ==================================================
echo.
echo Pipeline:
echo   1. Extract frames from videos\
echo   2. Auto-label with teacher (yolov8x at imgsz=1280)
echo   3. Mine negatives from raw_frames\negatives\ (optional)
echo   4. Hash-split train/valid/test (80/15/5, no leakage)
echo   5. Validate dataset
echo   6. Train (imgsz=640, mosaic+mixup+copy-paste)
echo   7. Export NCNN at imgsz=256 (FP16) -^> outputs\export\
echo   8. Active-learning sweep -^> outputs\review\uncertain\
echo.
echo State is checkpointed at each step; re-run to resume.
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

python src\automate.py %*
set RC=%ERRORLEVEL%

if %RC% EQU 0 (
  echo.
  echo Automation finished successfully.
  echo - Reports:        training\outputs\reports
  echo - Weights:        training\outputs\runs\detect\train\weights\best.pt
  echo - NCNN model:     training\outputs\export
  echo - Next-iter pool: training\outputs\review\uncertain
) else (
  echo.
  echo Automation finished with exit code %RC%.
  echo See training\outputs\reports\automate_state.json
)

exit /b %RC%
