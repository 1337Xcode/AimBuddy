@echo off
setlocal
cd /d "%~dp0.."

echo ==================================================
echo [07] Full Pipeline: preflight -> validate -> train -> export
echo ==================================================

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

python src\run_pipeline.py %*
set RC=%ERRORLEVEL%

if %RC% EQU 0 (
  echo.
  echo Pipeline completed successfully.
  echo - Reports: training\outputs\reports
  echo - Weights: training\outputs\runs\detect\train\weights
  echo - NCNN:    training\outputs\export
) else (
  echo.
  echo Pipeline failed with exit code %RC%.
  echo See training\outputs\reports\pipeline_last_run.json
  echo See training\outputs\reports\pipeline_last_run.log
)

exit /b %RC%
