@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo [03] Validate Dataset
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

python src\validate_dataset.py %*
set RC=%ERRORLEVEL%
if %RC% NEQ 0 (
  echo ERROR: Dataset validation failed with code %RC%.
  echo See training\outputs\reports\dataset_report.json
)
exit /b %RC%
