#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

from training_config import load_config
from pipeline_common import ensure_workspace_structure, write_json


SUPPORTED_PYTHON_MIN = (3, 10)
SUPPORTED_PYTHON_MAX_EXCLUSIVE = (3, 13)


def _ram_gb() -> float:
    try:
        import psutil  # type: ignore

        return round(psutil.virtual_memory().total / (1024 ** 3), 2)
    except Exception:
        if os.name == "nt":
            import ctypes

            class MemoryStatus(ctypes.Structure):
                _fields_ = [
                    ("dwLength", ctypes.c_ulong),
                    ("dwMemoryLoad", ctypes.c_ulong),
                    ("ullTotalPhys", ctypes.c_ulonglong),
                    ("ullAvailPhys", ctypes.c_ulonglong),
                    ("ullTotalPageFile", ctypes.c_ulonglong),
                    ("ullAvailPageFile", ctypes.c_ulonglong),
                    ("ullTotalVirtual", ctypes.c_ulonglong),
                    ("ullAvailVirtual", ctypes.c_ulonglong),
                    ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
                ]

            status = MemoryStatus()
            status.dwLength = ctypes.sizeof(MemoryStatus)
            ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
            return round(status.ullTotalPhys / (1024 ** 3), 2)
        return 0.0


def _nvidia_gpu_info_fallback() -> tuple[bool, str, float | None]:
    try:
        proc = subprocess.run(
            [
                "nvidia-smi",
                "--query-gpu=name,memory.total",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )

        if proc.returncode != 0:
            return False, "cpu", None

        first = ""
        for line in proc.stdout.splitlines():
            if line.strip():
                first = line.strip()
                break

        if not first:
            return False, "cpu", None

        parts = [p.strip() for p in first.split(",")]
        name = parts[0] if parts else "NVIDIA GPU"
        vram_gb = None
        if len(parts) > 1:
            try:
                vram_gb = round(float(parts[1]) / 1024.0, 2)
            except ValueError:
                vram_gb = None

        return True, name, vram_gb
    except Exception:
        return False, "cpu", None


def _gpu_info() -> tuple[bool, str, float | None, bool, str]:
    try:
        import torch  # type: ignore

        if not torch.cuda.is_available():
            nvidia_present, nvidia_name, nvidia_vram = _nvidia_gpu_info_fallback()
            if nvidia_present:
                return False, nvidia_name, nvidia_vram, True, "torch_cuda_unavailable"
            return False, "cpu", None, False, "no_gpu"

        device_name = torch.cuda.get_device_name(0)
        vram = torch.cuda.get_device_properties(0).total_memory / (1024 ** 3)
        return True, device_name, round(vram, 2), True, "cuda_ready"
    except Exception:
        nvidia_present, nvidia_name, nvidia_vram = _nvidia_gpu_info_fallback()
        if nvidia_present:
            return False, nvidia_name, nvidia_vram, True, "torch_missing_or_incompatible"
        return False, "cpu", None, False, "no_gpu"


def _python_supported_for_training() -> bool:
    ver = sys.version_info
    return (ver.major, ver.minor) >= SUPPORTED_PYTHON_MIN and (ver.major, ver.minor) < SUPPORTED_PYTHON_MAX_EXCLUSIVE


def _evaluate_cpu_ram_disk(cpu_cores: int, ram_gb: float, disk_free_gb: float) -> tuple[list[str], list[str]]:
    minimum_failures: list[str] = []
    warnings: list[str] = []

    if cpu_cores < 4:
        minimum_failures.append("Minimum CPU is 4 logical cores")
    elif cpu_cores < 8:
        warnings.append("CPU below recommended 8+ logical cores")

    if ram_gb < 7.5:
        minimum_failures.append("Minimum RAM is 8 GB")
    elif ram_gb < 15.5:
        warnings.append("RAM below recommended 16+ GB")

    if disk_free_gb < 15:
        minimum_failures.append("Minimum free disk is 15 GB")
    elif disk_free_gb < 30:
        warnings.append("Free disk below recommended 30+ GB")

    return minimum_failures, warnings


def _gpu_warnings(
    has_gpu: bool,
    gpu_vram_gb: float | None,
    nvidia_present: bool,
) -> list[str]:
    warnings: list[str] = []

    if has_gpu:
        if gpu_vram_gb is not None and gpu_vram_gb < 4:
            warnings.append("GPU VRAM below recommended 4+ GB for faster training")
        return warnings

    if nvidia_present:
        warnings.append(
            "NVIDIA GPU detected but CUDA is unavailable in this Python environment. "
            "Common fix: use Python 3.10-3.12 and install CUDA-enabled torch wheels."
        )
        if sys.version_info >= (3, 13):
            warnings.append(
                "Current Python version is too new for common CUDA torch builds. "
                "Create a Python 3.11 venv for GPU training."
            )
        return warnings

    warnings.append("No CUDA-capable GPU detected. Training will run on CPU and be much slower")
    return warnings


def run_preflight(config_path: Path | None = None, strict: bool = True) -> int:
    cfg = load_config(config_path)
    ensure_workspace_structure(cfg)

    cpu_cores = os.cpu_count() or 1
    ram_gb = _ram_gb()
    disk_free_gb = round(shutil.disk_usage(cfg.paths.root_dir).free / (1024 ** 3), 2)
    python_version = platform.python_version()
    py_ok = _python_supported_for_training()

    has_gpu, gpu_name, gpu_vram_gb, nvidia_present, gpu_status = _gpu_info()

    minimum_failures, warnings = _evaluate_cpu_ram_disk(cpu_cores, ram_gb, disk_free_gb)

    if not py_ok:
        warnings.append("Python 3.10-3.12 is recommended for stable ultralytics/torch CUDA support")

    warnings.extend(_gpu_warnings(has_gpu, gpu_vram_gb, nvidia_present))

    report = {
        "python_version": python_version,
        "python_supported": py_ok,
        "cpu_logical_cores": cpu_cores,
        "ram_gb": ram_gb,
        "disk_free_gb": disk_free_gb,
        "gpu_detected": has_gpu,
        "nvidia_gpu_present": nvidia_present,
        "gpu_status": gpu_status,
        "gpu_name": gpu_name,
        "gpu_vram_gb": gpu_vram_gb,
        "minimum_failures": minimum_failures,
        "warnings": warnings,
    }

    report_path = cfg.paths.output_dir / "reports" / "preflight_report.json"
    write_json(report_path, report)

    print("Hardware preflight summary")
    print(f"  Python: {python_version}")
    print(f"  CPU cores: {cpu_cores}")
    print(f"  RAM GB: {ram_gb}")
    print(f"  Free disk GB: {disk_free_gb}")
    print(f"  Compute: {gpu_name} ({gpu_status})")
    print(f"  Report: {report_path}")

    for warning in warnings:
        print(f"WARNING: {warning}")

    if strict and minimum_failures:
        print("ERROR: system does not meet minimum training requirements")
        for failure in minimum_failures:
            print(f"  - {failure}")
        return 2

    if minimum_failures:
        print("WARNING: minimum checks failed but strict mode is disabled")
        for failure in minimum_failures:
            print(f"  - {failure}")

    print("Preflight check passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Check hardware/software minimum requirements before training")
    parser.add_argument("--config", type=Path, default=None, help="Path to config/config.ini")
    parser.add_argument("--non-strict", action="store_true", help="Warn on minimum failures instead of exiting")
    args = parser.parse_args()
    return run_preflight(args.config, strict=not args.non_strict)


if __name__ == "__main__":
    raise SystemExit(main())
