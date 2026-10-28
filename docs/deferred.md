# Deferred

What AimBuddy does not do yet, and what it would take. Each entry says why it was left out, so the next person does not have to find out again.

## A game whose look axis is reversed

The assist takes it that moving the finger up turns the camera up, and left turns it left. Only the vertical axis has a switch (Invert vertical, under Game camera). A game that turns the camera the other way for a finger movement on the horizontal axis sends the aim away from the target. In the simulation, with a camera gain of -1.5, the assist pushed the crosshair 490 px the wrong way and ended 650 px off the target. Calibration measures how far the camera moves for a finger movement, but it assumes the direction.

Why it waits: a signed gain touches the camera estimator, the camera model and the controller, and each of them assumes a gain above zero. To do it, let the estimator learn the sign (the slide it measures between two frames has a direction), carry the sign through the controller, and add simulated games with each axis reversed to the tests.

## Streamer mode that shows the overlay to the player only

As built, streamer mode draws nothing over the game, so nothing of the overlay can be recorded. The player sees the boxes and the fields of view only with streamer mode off. Showing them to the player while a recording shows the game alone needs a layer that screen capture leaves out. An app has no public way to ask for one. The one flag it can set, secure, makes the compositor paint the layer black in the captured picture instead of leaving it out: with the menu open in streamer mode, a screenshot on the emulator was half black (49.5% in landscape), which is why the secure panel covers only the menu. With Shizuku or root a layer like that might be reachable. That would not work on every phone and has not been tried.

## Other ways to the touchscreen

Built: the touch helper makes a virtual touchscreen that mirrors the real one and adds one finger. Shizuku or root starts it. Not built:

- **Writing the assist's finger into the real touchscreen's event file as root, without taking the touchscreen over.** Some of the reference tools do it. It would skip the mirror, but the driver and the injected finger would share one table of contact slots and neither knows what the other put in it, so a real finger can be overwritten or left stuck. The mirror exists to avoid that. Not tried.
- **An accessibility service that dispatches gestures.** The player would have to turn on an accessibility service, and its gestures are strokes with a path and a duration fixed when they are sent, where the assist needs a finger that follows a moving target frame by frame beside the player's own finger. Not tried.
- **A kernel driver or module.** It needs a custom kernel for each phone.

## Left out on purpose

Reading the game's memory, hiding the app or the virtual touchscreen from the game (random device names and ids), and pulling the trigger for the player. Some of the reference tools do these. AimBuddy does not.
