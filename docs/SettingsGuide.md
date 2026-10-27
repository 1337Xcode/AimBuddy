# Settings guide

Every setting AimBuddy has, what it does, and what to try when something is off. Most settings are on the Settings screen of the app. The in-game menu, opened from the round button over the game (or from the notification's Menu button), has the same settings and a few more, and a change in either shows in both. A change takes effect at once.

Where the settings are kept, and the limits each value is held to, are in `app/src/main/cpp/settings/settings.h` and `settings_store.cpp`.

## Overlay

| Setting | Default | What it does |
|---------|---------|--------------|
| Box color | Red | The color of the outline around each target. Ten colors. |
| Line thickness | 2 px | 1 to 6 px. |
| Confidence labels | On | The detector's confidence next to each box. |
| Aim point | On | A marker where the assist aims. |
| Line to nearest target | Off | A line from the crosshair to the nearest target. |
| Target count | Off | How many targets are in view, at the top of the screen. |
| Performance readout | Off | Top right: how many detections finish each second, how old the picture is when its targets are ready, and whether the CPU or the GPU runs the model. Use it to judge whether a setting is too heavy for the phone. |

The guides for setting up (the crosshair, the detection square and the reach circle) are drawn only while the menu or the touch area editor is open, so they never sit on top of the game and inside the picture the detector reads.

## Detection

| Setting | Default | What it does |
|---------|---------|--------------|
| Detection confidence | 50% | The lowest score that starts a target, 10% to 95%. Lower finds more targets and more false ones. |
| Detection area | 200 px | The radius around the crosshair that the detector looks at, 100 to 650 px. A smaller area makes targets look larger to the detector, so far ones are easier to find; targets outside it are not seen. The square it makes is never smaller than the model's input or larger than 1.75 times it, so the ends of the slider can look alike. |

## Aim assist

The assist starts off every time AimBuddy runs, because it moves the aim by itself. Switch it on from the Aim tab of the in-game menu once you are in the game.

| Setting | Default | What it does |
|---------|---------|--------------|
| Aim at | Head | Which part of a target to steer towards: head, chest or centre. When the model detects heads, a head is aimed at exactly. Otherwise the point is worked out from the shape of the visible box: near the top for a figure seen head to foot, nearer the middle for one that is partly hidden. |
| Strength | 60% | How quickly the aim closes on a target. Higher closes faster and can overshoot; lower is calmer. |
| Reach | 220 px | Targets farther than this from the crosshair are left alone. |
| Target pick | Crosshair | Which target to take when there are several: the one nearest the crosshair, the biggest, or the one the detector is surest of. The assist keeps the target it has until a better one is clearly better, so it does not hop between two that are almost equally good. |

## Game camera

How far the view turns for each pixel the assist's finger moves, and how long the game takes to react, differ a lot between games and sensitivity settings. AimBuddy measures both while you play.

| Setting | Default | What it does |
|---------|---------|--------------|
| Calibrate automatically | On | Measure the game's answer to the assist's finger instead of trusting the two numbers below. What it found is saved and used as a first guess next time, never as a fact. |
| Camera sensitivity | 1.5 | The first guess for how far the scene slides per finger pixel. Used until the measurement is trustworthy, and always when calibration is off. |
| Response delay | 50 ms | The first guess for how long the game takes to react. |
| Invert vertical | Off | For a game that looks up when the finger moves down. |

If the aim overshoots or crawls with calibration on, give it a minute of aiming, because the first moves are small on purpose. If it still does, turn calibration off and set the two numbers by hand.

## Touch

| Setting | Default | What it does |
|---------|---------|--------------|
| Touch access | Automatic | How AimBuddy reaches the touchscreen: Shizuku, root, or automatic, which prefers Shizuku. See [Shizuku setup](ShizukuSetup.md). |

The touch area is where the assist's finger rests. Move and resize it from the Aim tab of the in-game menu while AimBuddy is running, so that it is away from your own thumbs. The assist's finger never travels farther than the area's radius from its centre. The menu also resets the area to where it started.

## App

| Setting | Default | What it does |
|---------|---------|--------------|
| Language | English | English, Chinese or Russian, for the app and the in-game menu together. |
| Appearance | System | Follow the phone, or always dark or light. |
| Accent color | Blue | The color of buttons and highlights in the app. Ten colors. |
| Streamer mode | Off | Draws nothing over the game, makes the floating button invisible, and keeps the menu out of screenshots and recordings. Aim assist keeps working. The button stays where it is: tap there to open the menu and hold to stop. The notification has a Menu button too. While the menu is open a recording shows black. |

## About

The version, whether a newer one is available, and a switch for checking by itself when the app opens. There are also links to the source code and the license, and the list of open source notices. Reset all settings puts everything back to how it started and keeps the language.

## Where the settings live

One file, `settings.cfg`, in the app's private storage, with a `name=value` line per setting. Names the app does not know are ignored and missing ones keep their default, so a file from another version still loads. The assist's on/off switch is not saved.

## Tuning

1. Start with the defaults and use the performance readout. If detections per second are low, read [Performance](Performance.md) first: the detection area does not change how fast the detector runs.
2. Raise the confidence if boxes appear on things that are not enemies; lower it if enemies are missed.
3. Pick the target pick and aim point that suit the game, then set the strength to taste.
4. Leave calibration on. Turn it off only if the aim keeps missing after a minute of aiming.
