@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo [04] Train Adaptive
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

python src\preflight_check.py
if errorlevel 1 (
  echo ERROR: Preflight failed. See training\outputs\reports\preflight_report.json
  exit /b 1
)

python src\train.py --adaptive %*
set RC=%ERRORLEVEL%
if %RC% NEQ 0 (
  echo ERROR: Training failed with code %RC%.
  echo See training\outputs\reports\selected_training_config.json
)
exit /b %RC%
