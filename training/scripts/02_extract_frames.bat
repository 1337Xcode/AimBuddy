@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo [02] Extract Frames
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

python src\preflight_check.py --non-strict
if errorlevel 1 (
  echo ERROR: Preflight execution failed.
  exit /b 1
)

python src\extract_frames.py %*
set RC=%ERRORLEVEL%
if %RC% NEQ 0 (
  echo ERROR: Frame extraction failed with code %RC%.
)
exit /b %RC%
