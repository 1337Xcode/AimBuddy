#!/usr/bin/env python3

from __future__ import annotations

import argparse
from dataclasses import asdict
from pathlib import Path
import traceback

from preflight_check import run_preflight
from validate_dataset import run_validation
from train import run_training
from export_to_ncnn import run_export
from training_config import load_config
from pipeline_common import ensure_workspace_structure, write_json, append_log_line, utc_now_iso


def _step(name: str, fn, config_path: Path | None, adaptive_override: bool | None = None) -> tuple[int, dict]:
    started = utc_now_iso()
    try:
        if adaptive_override is None:
            rc = fn(config_path)
        else:
            rc = fn(config_path, adaptive_override)
    except Exception as exc:
        return 99, {
            "step": name,
            "started_at": started,
            "finished_at": utc_now_iso(),
            "exit_code": 99,
            "error": f"{type(exc).__name__}: {exc}",
            "traceback": traceback.format_exc(),
        }

    return rc, {
        "step": name,
        "started_at": started,
        "finished_at": utc_now_iso(),
        "exit_code": rc,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="One-command preflight, validation, training, and NCNN export")
    parser.add_argument("--config", type=Path, default=None, help="Path to config/config.ini")
    parser.add_argument("--manual", action="store_true", help="Disable adaptive auto-selection")
    parser.add_argument("--adaptive", action="store_true", help="Force adaptive auto-selection")
    parser.add_argument("--skip-export", action="store_true", help="Skip NCNN export step")
    parser.add_argument("--non-strict-preflight", action="store_true", help="Warn instead of failing for minimum hardware check")
    args = parser.parse_args()

    if args.manual and args.adaptive:
        print("ERROR: choose either --manual or --adaptive")
        return 2

    adaptive_override = None
    if args.manual:
        adaptive_override = False
    if args.adaptive:
        adaptive_override = True

    cfg = load_config(args.config)
    ensure_workspace_structure(cfg)

    reports_dir = cfg.paths.output_dir / "reports"
    report_path = reports_dir / "pipeline_last_run.json"
    log_path = reports_dir / "pipeline_last_run.log"

    run_report = {
        "started_at": utc_now_iso(),
        "config": str((args.config or (cfg.paths.root_dir / "config" / "config.ini")).resolve()),
        "flags": {
            "manual": args.manual,
            "adaptive": args.adaptive,
            "skip_export": args.skip_export,
            "non_strict_preflight": args.non_strict_preflight,
        },
        "resolved_paths": {key: str(value) for key, value in asdict(cfg.paths).items()},
        "steps": [],
        "status": "running",
    }

    append_log_line(log_path, "=" * 70)
    append_log_line(log_path, f"Pipeline started at {run_report['started_at']}")

    steps: list[tuple[str, object, bool]] = [
        ("preflight", run_preflight, False),
        ("validate_dataset", run_validation, False),
        ("train_model", run_training, True),
    ]

    if not args.skip_export:
        steps.append(("export_ncnn", run_export, False))

    for step_name, fn, needs_adaptive in steps:
        print(f"Running step: {step_name}")

        if step_name == "preflight":
            rc, detail = _step(step_name, lambda c: run_preflight(c, strict=not args.non_strict_preflight), args.config)
        elif needs_adaptive:
            rc, detail = _step(step_name, fn, args.config, adaptive_override)
        else:
            rc, detail = _step(step_name, fn, args.config)

        run_report["steps"].append(detail)
        append_log_line(log_path, f"{step_name} => rc={rc}")

        if rc != 0:
            run_report["status"] = "failed"
            run_report["failed_step"] = step_name
            run_report["finished_at"] = utc_now_iso()
            write_json(report_path, run_report)
            print(f"ERROR: pipeline failed at step '{step_name}'")
            print(f"Detailed report: {report_path}")
            print(f"Detailed log: {log_path}")
            return rc

    run_report["status"] = "success"
    run_report["finished_at"] = utc_now_iso()
    write_json(report_path, run_report)

    append_log_line(log_path, f"Pipeline completed at {run_report['finished_at']}")
    print("Pipeline completed successfully")
    print(f"Detailed report: {report_path}")
    print(f"Detailed log: {log_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
