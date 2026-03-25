# Performance Guide

This document defines how performance changes are evaluated in AimBuddy.

## Primary Objectives

- Keep frame pacing stable.
- Maintain tracking consistency under load.
- Minimize allocations in per-frame native paths.
- Preserve stable assisted input behavior during live setting changes.

## Critical Runtime Paths

- Detection inference and post-processing
- Target tracking and association updates
- Control loop computations (`AimbotController`)
- Overlay draw submission and UI rendering

## Measurement Standards

- Use measured counters, not UI estimates alone.
- Track FPS and frame time in milliseconds.
- Validate on representative Android devices and screen resolutions.
- Compare Visual Assist Mode and Assisted Input Mode separately.

## Performance Change Policy

- Every optimization requires before and after measurements.
- Reject speed changes that reduce tracking quality or stability.
- Avoid extra per-frame visual complexity unless measured value is clear.
- Keep logs out of hot loops unless temporary profiling is required.

## Practical Device Validation Matrix

Minimum validation set:

1. One mid-range arm64 Android device.
2. One higher-end arm64 Android device.
3. At least one long session test with multiple start and stop cycles.

Recommended measurements:

- p50 and p95 frame time
- detection update rate
- dropped-frame count during permission transitions

## Baseline Build Check

```powershell
./gradlew.bat clean assembleDebug
```

Run this build check with on-device profiling to validate runtime impact.
