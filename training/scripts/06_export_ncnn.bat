@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo [06] Export NCNN
 echo ============================================

if not exist ".venv\Scripts\python.exe" (
  echo ERROR: Environment missing. Run scripts\01_setup_environment.bat first.
  exit /b 1
)

call .venv\Scripts\activate.bat
if errorlevel 1 (
  echo ERROR: Failed to activate virtual environment.
  exit /b 1
)

python src\export_to_ncnn.py %*
set RC=%ERRORLEVEL%
if %RC% NEQ 0 (
  echo ERROR: Export failed with code %RC%.
  echo Check training\outputs\reports\pipeline_last_run.json if run from full pipeline.
)
exit /b %RC%
