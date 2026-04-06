# Settings Guide

How runtime settings control detection, tracking, aim assist, and overlay behavior in AimBuddy. All runtime settings live in the `UnifiedSettings` struct.

## Where Settings Are Defined

| Location | Purpose |
|----------|---------|
| `app/src/main/cpp/settings.h` | Compile-time constants (capture size, model config, NCNN flags) |
| `app/src/main/cpp/utils/aimbot_types.h` | Runtime settings struct (`UnifiedSettings`) with defaults and validation |
| `app/src/main/cpp/renderer/imgui_menu.cpp` | ImGui menu that writes settings live, includes preset buttons |

Settings are read by snapshot copy in hot-path threads to avoid contention.

## Validation

`UnifiedSettings::validate()` is called before every hot-path usage. It clamps all values to safe ranges:

| Parameter | Range | Default |
|-----------|-------|---------|
| aimbotFps | 30 to 120 | 60 |
| aimMode | 0 to 2 | 0 (Smooth) |
| filterType | 0 to 2 | 1 (EMA) |
| aimSpeed | 0.1 to 1.0 | 0.45 |
| smoothness | 0.0 to 1.0 | 0.78 |
| fovRadius | 50 to 600 | 200 |
| aimFovRadius | 50 to min(600, fovRadius) | 175 |
| confidenceThreshold | 0.1 to 0.95 | 0.5 |
| maxAimDistance | 100 to 1000 | 400 |
| touchRadius | 50 to 500 | 250 |
| aimDelay | 0 to 50 | 3.5 |
| emaAlpha | 0.08 to 0.90 | 0.25 |
| velocityLeadFactor | 0.0 to 0.8 | 0.28 |
| velocityLeadClamp | 1 to 40 | 18 |
| pdDerivativeGain | 0.0 to 0.35 | 0.045 |
| maxLostFrames | 1 to 30+ | 8 |
| maxLockMissFrames | 1 to 30 | 2 |
| targetSwitchDelayFrames | 0 to 30 | 6 |
| velocitySmoothing | 0.05 to 0.95 | 0.55 |
| boxThickness | 1 to 10 | 2 |
| smoothingFactor | 0.1 to 1.0 | 0.30 |
| touchZoneAlpha | 0.1 to 1.0 | 0.3 |
| convergenceRadius | 10 to 100 (recommended) | 30 |
| recoilCompensationStrength | 0.0 to 1.5 | 0.18 |
| recoilCompensationMax | 2 to 60 | 12 |
| recoilCompensationDecay | 0.50 to 0.98 | 0.84 |
| kalmanProcessNoise | 0.01 to 20.0 | 1.0 |
| kalmanMeasurementNoise | 0.5 to 40.0 | 4.0 |

Boolean defaults that are often tuned together:

- `enableConvergenceDamping`: true
- `recoilCompensationEnabled`: false
- `showTouchZone`: true

Deprecated setting note:

- `enableKalmanFilter` is deprecated and retained for compatibility. Use `filterType` (`0=None`, `1=EMA`, `2=Kalman`) as the active filter selector.

## Presets

Four built-in presets configure multiple settings at once. Select a preset as a starting point, then fine-tune individual values.

### Default

General-purpose balanced profile.

| Setting | Value |
|---------|-------|
| Aim mode | Smooth |
| Speed / Smoothness | 0.48 / 0.78 |
| Filter | EMA (alpha 0.25) |
| Velocity lead | 0.22 (clamp 18) |
| Aim FOV | 240 px |
| Head offset | 0.18 |
| Miss grace / Switch delay | 2 / 8 |

### Competitive

Fast acquisition with snap aim for quick reactions.

| Setting | Value |
|---------|-------|
| Aim mode | Snap |
| Speed / Smoothness | 0.72 / 0.45 |
| Filter | None |
| Velocity lead | 0.30 (clamp 20) |
| Aim FOV | 220 px |
| Head offset | 0.15 |
| Miss grace / Switch delay | 2 / 5 |

### Balanced

Smoother and more cautious than Default, better for close-range stability.

| Setting | Value |
|---------|-------|
| Aim mode | Smooth |
| Speed / Smoothness | 0.52 / 0.80 |
| Filter | EMA (alpha 0.22) |
| Velocity lead | 0.24 (clamp 16) |
| Aim FOV | 260 px |
| Head offset | 0.18 |
| Miss grace / Switch delay | 2 / 9 |

### Precision

Kalman-filtered magnetic aim for maximum lock stability.

| Setting | Value |
|---------|-------|
| Aim mode | Magnetic |
| Speed / Smoothness | 0.58 / 0.88 |
| Filter | Kalman (process 0.8, measure 5.0) |
| Velocity lead | 0.18 (clamp 14) |
| Aim FOV | 300 px |
| Head offset | 0.17 |
| Miss grace / Switch delay | 2 / 12 |

## Setting Groups

### Enable/Disable Controls

- `aimbotEnabled`: Enables the assisted input pipeline. ESP overlays remain active regardless.
- `espEnabled`: Enables visual overlays.
- `showDetectionCount`: Shows enemy count at top of screen.
- `showLabels`: Shows "Enemy" label and confidence percentage on each box.

### Detection Zone

- `fovRadius`: Size of the center crop region used for detection. Larger scans more area but is slower.
- `confidenceThreshold`: Minimum confidence to display a detection and consider it for targeting.

### Target Selection

- `aimFovRadius`: Only targets inside this radius (from screen center) are candidates for aim assist. Must be less than or equal to `fovRadius`.
- `maxAimDistance`: Maximum pixel distance for aim engagement.
- `targetPriority`: How to rank candidates. 0 = nearest to crosshair, 1 = largest box, 2 = highest confidence.
- `headPriority`: When enabled, aim point targets the head region instead of box center.
- `headOffset`: Position within the box (0.0 = top edge, 0.5 = center, 1.0 = bottom edge). Default 0.2 targets the head/neck area.

### Aim Modes

- `aimMode 0 (Smooth)`: PD controller with convergence damping. Natural and stable. Best general-purpose mode.
- `aimMode 1 (Snap)`: Gain-capped proportional movement. Fast acquisition, never overshoots in a single frame.
- `aimMode 2 (Magnetic)`: Distance-proportional pull. Very gentle near-lock, smooth approach.

### Speed and Smoothness

- `aimSpeed`: Movement rate multiplier. Higher = faster corrections but potentially more overshoot.
- `smoothness`: Dampening factor. Higher = smoother and more natural, but slower to acquire targets.
- `aimbotFps`: Update rate for the aim control loop (separate from inference rate). Higher is more responsive.

### Aim Stabilization Filters

Three filter types smooth the aim point to absorb detector jitter:

| Filter | Setting | Behavior |
|--------|---------|----------|
| None (0) | No filtering | Raw detection positions, most responsive but jittery |
| EMA (1) | `emaAlpha` | Exponential moving average. Lower alpha = smoother but more lag |
| Kalman (2) | `kalmanProcessNoise`, `kalmanMeasurementNoise` | Statistically optimal filter. Higher process noise = more responsive |

### Velocity Lead

Velocity lead predicts where a moving target will be:

- `velocityLeadFactor`: How much to lead the target based on its tracked velocity. 0 = no lead.
- `velocityLeadClamp`: Maximum lead offset in pixels per axis. Prevents over-prediction.

Lead is scaled proportionally by target speed (faster targets get more lead) and gated by confidence and distance from crosshair.

### Convergence Damping

- `enableConvergenceDamping`: Reduces aim speed when close to the target to prevent overshoot.
- `convergenceRadius`: Distance (in pixels) at which damping begins. Smaller = tighter but more oscillation risk.

### Derivative Damping

- `pdDerivativeGain`: Scales the derivative (velocity) brake in smooth mode. Higher = more oscillation dampening but slower reactions.

### Recoil Compensation

- `recoilCompensationEnabled`: Adds upward Y correction when the target drifts downward (simulating recoil pull).
- `recoilCompensationStrength`: Correction scaling factor.
- `recoilCompensationMax`: Maximum Y correction per frame in pixels.
- `recoilCompensationDecay`: How quickly the integrator decays (lower = faster response).

### Tracking Parameters

- `maxLostFrames`: How many inference frames a track survives without a matching detection (default 8).
- `maxLockMissFrames`: How many frames the locked target can be lost before releasing aim (default 2).
- `targetSwitchThreshold`: New target must be this factor better than the locked target to trigger a switch (default 1.3).
- `targetSwitchDelayFrames`: Cooldown frames after switching targets (default 6).

### Touch Injection

- `touchX`, `touchY`: Center of the touch injection zone on screen.
- `touchRadius`: Maximum distance touch events can travel from the center.
- `aimDelay`: Additional delay in ms between touch movements (0 for fastest).

### ESP Rendering

- `boxColorR/G/B`: RGB color of detection boxes (0.0 to 1.0).
- `boxThickness`: Box border thickness in pixels (1 to 10).
- `drawLine`: Enable snap line from crosshair to nearest target.
- `drawDot`: Show head position dot on each detection.
- `enableSmoothing`: Enable temporal box smoothing for the ESP overlay (separate from aimbot filtering).
- `smoothingFactor`: ESP box smoothing strength (lower = smoother).
- `showTouchZone`: Show the touch injection zone circle on the overlay.
- `touchZoneAlpha`: Opacity of the touch zone overlay.

## Persistence

- Settings are serialized as a binary blob to `/data/local/tmp/settings.bin`.
- A magic number (`0xE5BA1005`) validates file integrity on load.
- `reset()` restores all defaults while preserving screen dimensions.
- Auto-save triggers shortly after any menu edit (when no slider is active).
- The "Save now" button forces an immediate write.

## Safe Tuning Workflow

1. Start with a preset that matches your play style.
2. Adjust one parameter group at a time.
3. Test stability: does the aim hold steady on a still target?
4. Test tracking: does the aim follow a moving target smoothly?
5. Test release: does touch stop promptly when looking away from enemies?
6. If unstable, select a preset to reset to known-good values.

## Related Documentation

- [Architecture](Architecture.md)
- [Performance](Performance.md)
- [Training](Training.md)
- [Troubleshooting](Troubleshooting.md)
