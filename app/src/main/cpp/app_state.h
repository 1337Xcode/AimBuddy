#ifndef AIMBUDDY_APP_STATE_H
#define AIMBUDDY_APP_STATE_H

#include <string>

#include "pipeline/aim_loop.h"
#include "pipeline/detection_pipeline.h"
#include "settings/settings_store.h"
#include "touch/touch_link.h"

namespace aimbuddy {

/**
 * The app's long-lived parts, each created once.
 *
 * The overlay, the glue to Kotlin and the aim loop live in different files from the code that
 * creates them, so they reach them through these functions instead of each holding a pointer.
 * They are defined next to the code that owns them (esp_jni.cpp).
 */
DetectionPipeline& pipeline();
SettingsStore& settingsStore();
touch::TouchLink& touchLink();
AimLoop& aimLoop();

/// Where the settings file lives (in the app's private storage).
const std::string& settingsPath();

}  // namespace aimbuddy

#endif  // AIMBUDDY_APP_STATE_H
