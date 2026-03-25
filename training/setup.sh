#!/bin/bash
# ============================================================================
# setup.sh - Linux/macOS setup script for ESP training pipeline
# Creates virtual environment and installs all dependencies
# ============================================================================

cd "$(dirname "$0")"

echo "============================================"
echo "ESP Training Pipeline Setup (Linux/macOS)"
echo "============================================"
echo ""

# Check Python installation
if ! command -v python3 &> /dev/null; then
    echo "ERROR: Python 3 is not installed"
    echo "Please install Python 3.8+ from your package manager"
    exit 1
fi

echo "[1/4] Creating Python virtual environment..."
python3 -m venv .venv
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to create virtual environment"
    exit 1
fi

echo "[2/4] Activating virtual environment..."
source .venv/bin/activate

echo "[3/4] Upgrading pip..."
pip install --upgrade pip

echo "[4/4] Installing dependencies..."
pip install -r requirements.txt

if [ $? -ne 0 ]; then
    echo ""
    echo "ERROR: Failed to install some dependencies"
    echo "You may need to install PyTorch manually for your CUDA version:"
    echo "  pip install torch torchvision --index-url https://download.pytorch.org/whl/cu118"
    echo ""
    exit 1
fi

echo "Ensuring base model (yolo26n.pt) is available..."
python3 src/download_base_model.py
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to fetch base model yolo26n.pt"
    exit 1
fi

echo ""
echo "============================================"
echo "Setup complete!"
echo "============================================"
echo ""
echo "Next steps:"
echo "  1. Place YOLO dataset under dataset/train, dataset/valid, dataset/test"
echo "  2. Run: python3 src/preflight_check.py"
echo "  3. Run: python3 src/run_pipeline.py"
echo ""
echo "To activate the environment later:"
echo "  source .venv/bin/activate"
echo ""
