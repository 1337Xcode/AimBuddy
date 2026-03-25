---
name: aimbuddy-ai-guardrails
description: High-rigor project context, architecture constraints, and coding rules for AimBuddy.
applyTo: "**"
---

# AimBuddy - AI Coding Guardrails

## Scope

These instructions apply to all implementation, debugging, refactoring, and documentation work in this repository.
When there is uncertainty, prioritize correctness, safety, and maintainability over speed.

## System Purpose

AimBuddy is an Android app that combines:

1. Kotlin UI and Android lifecycle control
2. Native C++ runtime via JNI (`app/src/main/cpp`)
3. Real-time overlay rendering and capture pipeline
4. Root-assisted input capabilities (optional mode)
5. Config-driven YOLO26n training and NCNN export pipeline (`training/`)

## Hard Architecture Constraints (Do Not Override)

### Runtime inference/export contract

- Model contract is `yolo26n`-based workflow in this repository.
- Keep the training + export pipeline aligned with NCNN deployment artifacts.
- Do not introduce unrelated inference stacks or parallel model formats unless explicitly requested.

### Android + JNI boundary

- Keep JNI names aligned to package/class (`com.aimbuddy`) and native declarations.
- `app/src/main/cpp/esp_jni.cpp` is the JNI bridge anchor; avoid scattered JNI entry points.
- If Kotlin class/package names change, update JNI symbols in the same change.

### Launch and permission safety

- Never block the main thread with long-running or potentially interactive permission/root checks.
- Overlay, MediaProjection, and service startup must be sequenced defensively and fail gracefully.
- Permission denial must not crash; show actionable feedback and keep app responsive.

### Non-root behavior

- If root capability is unavailable, preserve non-root-safe behavior (ESP/visual-only mode) without crashes.
- Never force root-only flows in code paths that should remain functional without root.

## Repository Module Map (Current)

### Android app

- Native runtime: `app/src/main/cpp/`
	- `aimbot/`, `capture/`, `detector/`, `input/`, `renderer/`, `utils/`
	- JNI bridge: `esp_jni.cpp`
- Kotlin app layer: `app/src/main/java/com/aimbuddy/`

### Training stack

- Root: `training/`
- Config: `training/config/config.ini`
- Python entrypoints: `training/src/`
- Numbered user flow scripts: `training/scripts/01..07_*.bat`
- Outputs: `training/outputs/{reports,runs,export}`

Keep this organization intact unless a task explicitly requests structural changes.

## Engineering Principles

1. Root-cause fixes over local patches
2. KISS first, then abstraction only when needed
3. DRY through reuse of existing utilities
4. SOLID where practical, especially single-responsibility boundaries
5. No speculative rewrites in stable subsystems

## C++ Runtime Rules

- Preserve hot-path performance in capture, detector, tracker/controller, and renderer loops.
- Avoid unnecessary heap churn and repeated allocations in per-frame paths.
- Keep timing and synchronization logic deterministic and cheap.
- Do not introduce expensive logging in high-frequency loops.
- Match existing local style and naming in each C++ module.

## Kotlin/Android Rules

- No blocking operations on main/UI thread.
- Lifecycle transitions (`onCreate`, permission callbacks, service startup/shutdown) must be race-safe.
- Handle exception paths with user-visible status/toast/log rather than process crashes.
- Preserve stable overlay behavior during orientation/focus changes.

## Training Pipeline Rules

- Keep script chronology and UX simple for average users (`01` to `07`).
- Every training entrypoint must auto-create required folders when missing.
- Failures must emit clear, actionable messages and write reports/logs to `training/outputs/reports`.
- Support both adaptive and manual training paths from config.
- Keep docs and scripts synchronized when behavior changes.

## Documentation Rules

- Write concise, practical docs with step-by-step execution where relevant.
- Avoid placeholder or marketing language; prioritize operational clarity.
- If training/runtime behavior changes, update corresponding docs in same PR/change.

## Forbidden Patterns

- No placeholder implementations, TODO-only stubs, or dead/commented code blocks.
- No unrelated formatting-only churn in files not part of the task.
- No broad architecture changes without explicit request.
- No silent failure paths; always surface what failed and where to inspect details.

## Required Validation

### Android/native changes

Run:

```powershell
./gradlew.bat clean assembleDebug
```

### Training changes

- Run the affected script(s) from `training/scripts/`.
- At minimum verify setup/preflight and one target stage execute without path errors.
- Confirm required directories are created automatically if missing.

### Documentation changes

- Ensure links and referenced paths are valid in the current repository layout.

## Implementation Bias

- Prefer minimal, focused diffs.
- Preserve current behavior unless requirement says otherwise.
- If something is ambiguous, choose the safest simple interpretation and document assumptions.
