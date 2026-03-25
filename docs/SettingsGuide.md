# Settings Guide

This guide explains how runtime settings affect stability, detection quality, and assisted input behavior in AimBuddy.

Core rule: all runtime hot-path settings must pass `UnifiedSettings::validate()` before use.

## Where Settings Are Defined

- Defaults and constants: `app/src/main/cpp/settings.h`
- Runtime settings model: `app/src/main/cpp/utils/aimbot_types.h` (`UnifiedSettings`)
- Runtime writes from menu: `app/src/main/cpp/renderer/imgui_menu.cpp`
- Runtime reads in control paths:
  - `app/src/main/cpp/aimbot/target_tracker.cpp`
  - `app/src/main/cpp/aimbot/aimbot_controller.cpp`
  - `app/src/main/cpp/renderer/esp_renderer.cpp`

Note: `aimbot` path names are internal legacy identifiers. Documentation and UX describe these as assisted input and control settings.

## Validation and Safety Bounds

`UnifiedSettings::validate()` prevents unstable runtime states from live menu edits.

Important clamp examples:

- `aimbotFps`: `30..120`
- `aimMode`: `0..2`
- `filterType`: `0..2`
- `aimSpeed`: `0.1..1.0`
- `smoothness`: `0.0..1.0`
- `fovRadius`: `50..600`
- `aimFovRadius`: `50..600` and not above `fovRadius`
- `confidenceThreshold`: `0.1..0.95`
- `touchRadius`: `50..500`
- `aimDelay`: `0..50`
- `boxThickness`: `1..10`

Color channels and filter-control parameters are also clamped to safe ranges.

## Setting Groups

### Mode Controls

- `espEnabled`: enables visual overlays.
- `aimbotEnabled`: enables assisted input control path.
- `showDetectionCount`, `showLabels`, `enableSmoothing`: visual and UI behavior toggles.

### Detection and Target Acquisition

- `fovRadius`: primary detection and target region.
- `aimFovRadius`: tighter control region inside `fovRadius`.
- `maxAimDistance`: cap to prevent extreme target jumps.

### Motion Control

- `aimSpeed`, `smoothness`: movement rate and damping.
- `aimMode`: smooth, snap, or magnetic behavior.
- `targetPriority`: choose by distance, size, or confidence.

### Filtering and Prediction

- `filterType`: raw, EMA, or Kalman.
- `emaAlpha`: EMA responsiveness.
- `kalmanProcessNoise`, `kalmanMeasurementNoise`: Kalman tuning.
- `velocityLeadFactor`, `velocityLeadClamp`: bounded target lead.
- `pdDerivativeGain`: overshoot damping.

### Convergence and Recoil Compensation

- `enableConvergenceDamping`, `convergenceRadius`: slower near-lock movement.
- `recoilCompensationEnabled` and related parameters: bounded compensation controls.

### Rendering Controls

- `boxColorR/G/B`, `boxThickness`: overlay appearance.
- `confidenceThreshold`: draw and target inclusion threshold.
- `smoothingFactor`: visual interpolation strength.

## Persistence Model

- Settings are serialized to `/data/local/tmp/settings.bin` by default.
- Magic value `0xE5BA1005` protects payload compatibility.
- `reset()` restores defaults while keeping runtime screen dimensions.

## Safe Tuning Workflow

1. Change one parameter group at a time.
2. Keep a baseline preset for rollback.
3. Measure stability and latency together.
4. Retest start, stop, and restart behavior after major tuning changes.

## Related Documentation

- [Architecture](Architecture.md)
- [Performance](Performance.md)
- [Training](Training.md)
- [Troubleshooting](Troubleshooting.md)
