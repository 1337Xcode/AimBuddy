@echo off
setlocal
cd /d "%~dp0.."

echo ============================================
echo [01] Setup Environment
echo ============================================

set "PY311_EXE="
if exist "%LocalAppData%\Programs\Python\Python311\python.exe" set "PY311_EXE=%LocalAppData%\Programs\Python\Python311\python.exe"
if "%PY311_EXE%"=="" if exist "%ProgramFiles%\Python311\python.exe" set "PY311_EXE=%ProgramFiles%\Python311\python.exe"

if exist ".venv\Scripts\python.exe" goto ACTIVATE

set "PYTHON_CMD=python"
if not "%PY311_EXE%"=="" set "PYTHON_CMD=%PY311_EXE%"

if not "%PY311_EXE%"=="" echo Using Python 3.11: %PY311_EXE%
"%PYTHON_CMD%" --version >nul 2>&1
if errorlevel 1 (
  echo ERROR: No working Python interpreter found.
  echo Install Python 3.11 ^(recommended^) or add Python to PATH.
  exit /b 1
)

echo Creating virtual environment...
"%PYTHON_CMD%" -m venv .venv
if errorlevel 1 (
  echo ERROR: Failed to create virtual environment.
  exit /b 1
)

:ACTIVATE
if exist ".venv\Scripts\python.exe" echo Existing virtual environment found. Reusing .venv

call .venv\Scripts\activate.bat
if errorlevel 1 (
  echo ERROR: Failed to activate virtual environment.
  exit /b 1
)

python -m pip install --upgrade pip
if errorlevel 1 (
  echo ERROR: Failed to upgrade pip.
  exit /b 1
)

echo Installing PyTorch with CUDA 12.1 support...
python -m pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu121 --force-reinstall
if errorlevel 1 (
  echo ERROR: Failed to install PyTorch.
  exit /b 1
)

for /f %%v in ('python -c "import platform; print(platform.python_version())"') do set "ACTIVE_PYVER=%%v"
echo Active venv Python: %ACTIVE_PYVER%
echo NOTE: Python 3.10-3.12 is recommended for best CUDA/PyTorch compatibility.

pip install -r requirements.txt
if errorlevel 1 (
  echo ERROR: Failed to install requirements.
  exit /b 1
)

echo Ensuring base model ^(yolo26n.pt^) is available...
python src\download_base_model.py
if errorlevel 1 (
  echo ERROR: Failed to fetch base model yolo26n.pt.
  exit /b 1
)

if not exist outputs mkdir outputs
if not exist outputs\reports mkdir outputs\reports
if not exist outputs\runs mkdir outputs\runs
if not exist outputs\export mkdir outputs\export

python src\preflight_check.py --non-strict
if errorlevel 1 (
  echo ERROR: Preflight failed. See training\outputs\reports\preflight_report.json
  exit /b 1
)

echo Setup complete.
exit /b 0
