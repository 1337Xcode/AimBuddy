#ifndef ESP_RENDER_CONFIG_H
#define ESP_RENDER_CONFIG_H

#include <atomic>

#include "../settings.h"

namespace ESP {

/**
 * @struct RenderConfig
 * @brief Runtime rendering configuration (thread-safe)
 */
struct RenderConfig {
    std::atomic<float> boxColorR{static_cast<float>(Config::DEFAULT_BOX_COLOR_R) / 255.0f};
    std::atomic<float> boxColorG{static_cast<float>(Config::DEFAULT_BOX_COLOR_G) / 255.0f};
    std::atomic<float> boxColorB{static_cast<float>(Config::DEFAULT_BOX_COLOR_B) / 255.0f};
    std::atomic<int> boxThickness{Config::DEFAULT_BOX_THICKNESS};
    std::atomic<float> confidenceThreshold{Config::DEFAULT_CONFIDENCE_THRESHOLD};
    std::atomic<float> fovRadius{Config::DEFAULT_DETECTION_FOV_RADIUS};
    std::atomic<bool> showFPS{false};
    std::atomic<bool> showDetectionCount{false};
    std::atomic<bool> showLabels{true};
    std::atomic<bool> drawLine{Config::DEFAULT_DRAW_LINE};
    std::atomic<bool> drawDot{Config::DEFAULT_DRAW_DOT};
    std::atomic<bool> menuVisible{false};
    std::atomic<bool> enableSmoothing{false};   ///< Disabled for lowest latency
    std::atomic<float> smoothingFactor{0.45f};  ///< Balanced smoothness when enabled
    
    // Aimbot
    std::atomic<bool> aimbotEnabled{Config::DEFAULT_AIMBOT_ENABLED};
    std::atomic<float> headOffset{Config::DEFAULT_HEAD_OFFSET};
    std::atomic<int> targetLockFrames{Config::DEFAULT_TARGET_LOCK_FRAMES};
    
    // Touch Zone Settings (virtual joystick)
    std::atomic<float> touchCenterX{0.75f};  // Screen ratio (0.75 = 75% right)
    std::atomic<float> touchCenterY{0.5f};   // Screen ratio (0.5 = middle)
    std::atomic<float> touchRadius{150.0f};  // Pixels
    std::atomic<float> aimDelay{0.0f};       // Milliseconds between movements
};

} // namespace ESP

#endif // ESP_RENDER_CONFIG_H
