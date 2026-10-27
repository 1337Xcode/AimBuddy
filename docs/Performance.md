# Performance

What AimBuddy costs a phone, how to read the numbers it shows, and what to do when it is too much.

## Where the time goes

| Stage | What it costs | What you can do about it |
|-------|---------------|--------------------------|
| Capture | The system draws a second copy of the screen, at most 1600 pixels on the long edge. Only a small square around the crosshair is copied out of each frame. | Nothing in AimBuddy. A lower game frame rate or lower game graphics frees the GPU for it. |
| Detection | One run of the model on that square for every frame it can keep up with. This is the biggest cost. | A lighter model, if one is listed under Models. The CPU or the GPU is chosen for you, see below. |
| Camera motion | Both pictures are reduced to 64 by 64 grey values and compared. Small. | Nothing. |
| Tracking and aim | Small next to the detector. | Nothing. |
| Overlay | Boxes and text drawn with ImGui, at most 60 frames a second (90 while the menu is open). Streamer mode draws nothing. | Turn off what you do not need: labels, lines, count. |

## A slow phone drops frames, it does not fall behind

The capture hands each frame to the detector through a mailbox that keeps only the newest one. If the detector is still busy when the next frame arrives, the older frame is replaced. The targets AimBuddy reports are therefore never older than one detection, whatever the phone's speed, and a slow phone sees fewer updates a second instead of a growing delay.

## CPU or GPU

When the assist starts, the model is timed on the CPU and on the GPU (Vulkan), three warm-up runs and twelve timed ones each, and the median counts. The GPU is used only if it takes less than 80% of the CPU's time, because the game needs the GPU too. A phone without a working Vulkan driver uses the CPU.

On the CPU the model runs on at most three of the fast cores, and the little cores stay free for the game and the system. Idle worker threads sleep after one millisecond instead of spinning, which keeps the phone cooler between frames.

## When the phone gets hot

Android reports how hot the phone is. While AimBuddy runs it listens: when the phone is warm (the system's "moderate" level) AimBuddy runs at most 30 detections a second, and when it is hot ("severe" or worse) at most 15. It goes back to full speed once the phone has cooled down. Frames over the limit are skipped, not held back, so the targets AimBuddy reports are as fresh as ever, only fewer. The performance readout shows the lower rate while this lasts. A phone that stays cool is never slowed down.

## Reading the performance readout

Switch it on under Settings, Overlay, Performance readout. It sits at the top right of the screen and reads like `42/s  21 ms  GPU`.

| Part | Meaning |
|------|---------|
| `42/s` | Detections finished each second. The rate at which the screen updates is the ceiling, because a picture that did not change is not captured again. |
| `21 ms` | The age of a result when it is ready, counted from the moment its frame was on screen. This is the delay the aim has to make up for. Lower is better. |
| `GPU` | Which processor runs the model. |

If the rate is low or the age is high, the detector is the bottleneck. Close other apps, lower the game's graphics, or try a lighter model. The detection area has little effect on speed: the model always reads the same number of pixels, so a smaller area only makes targets larger in its eyes, and the area is never smaller than the model's input or larger than 1.75 times it.

## Reading the log

Debug builds log what the pipeline is doing every five seconds. Release builds log errors only.

```sh
adb logcat -s AimBuddy_Native:I
```

| Line | Meaning |
|------|---------|
| `Backend: ...` | Once, at start: the choice between CPU and GPU, with the time each took. |
| `Pipeline: ...` | Detections a second, inference time, frame age, how many frames were replaced by newer ones, and the processor in use. |
| `Last frame: ...` | How many targets the last frame had and the best score. |
| `Camera: ...` | How far the picture slid per frame on average, how well that could be measured, and what the assist has learned about the game's response. |

## Memory

The memory AimBuddy holds follows from a few fixed buffers: two capture buffers of the screen's size (at most 1600 pixels on the long edge, 4 bytes a pixel), three slots for the square around the crosshair (at most 448 by 448 pixels for the standard model), the model itself (about 10 MB for AimBuddy General) and the font atlas of the menu.

## Changing performance code

1. Measure before and after, with the readout on the same phone, in the same scene.
2. Reject a speed gain that costs tracking quality or steadiness. The tests in `app/src/test/native` show whether the aim still settles.
3. Keep logging out of the hot loops. The detection loop and the aim loop run every frame.
4. Check on at least one slow phone and one fast one.

## Related

- [Architecture](Architecture.md)
- [Settings guide](SettingsGuide.md)
- [Troubleshooting](Troubleshooting.md)
