#ifndef ESP_RENDERER_H
#define ESP_RENDERER_H

#include <atomic>
#include <array>
#include "overlay_window.h"
#include "box_smoothing.h"
#include "../detector/bounding_box.h"
#include "../detector/yolo_detector.h"
#include "../settings.h"
#include "../utils/logger.h"
#include "../utils/timer.h"

// ImGui includes
#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_opengl3.h"
#include "../imgui/imgui_impl_android.h"

/**
 * @file esp_renderer.h
 * @brief ImGui-based ESP overlay renderer
 * 
 * Handles drawing ESP boxes over detected enemies and provides
 * an ImGui menu for runtime configuration.
 */

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

/**
 * @class ESPRendere
 * @brief ImGui-based ESP overlay renderer
 */
class ESPRenderer {
public:
    ESPRenderer();
    ~ESPRenderer();
    
    // Non-copyable
    ESPRenderer(const ESPRenderer&) = delete;
    ESPRenderer& operator=(const ESPRenderer&) = delete;
    
    /**
     * @brief Initialize renderer with overlay window
     * @param window Initialized OverlayWindow
     * @return true if initialization successful
     */
    bool initialize(OverlayWindow* window);
    
    /**
     * @brief Shutdown renderer and release resources
     */
    void shutdown();
    
    /**
     * @brief Render one frame
     * @param result Detection results to render
     */
    void render(const DetectionResult& result);
    
    /**
     * @brief Handle touch input for ImGui
     * @param action Touch action (down=0, up=1, move=2)
     * @param x Touch X coordinate
     * @param y Touch Y coordinate
     * @return true if ImGui consumed the event
     */
    bool handleTouch(int action, float x, float y);
    
    /**
     * @brief Get render configuration (thread-safe)
     */
    RenderConfig& getConfig() { return config_; }
    
    /**
     * @brief Toggle menu visibility
     */
    void toggleMenu() {
        bool current = config_.menuVisible.load(std::memory_order_relaxed);
        config_.menuVisible.store(!current, std::memory_order_relaxed);
    }
    
    bool isMenuVisible() const {
        return config_.menuVisible.load(std::memory_order_relaxed);
    }
    
    /**
     * @brief Get FPS from timer
     */
    float getFPS() const { return timer_.getFPS(); }
    
    /**
     * @brief Check if renderer is initialized
     */
    bool isInitialized() const { return initialized_; }

private:
    /**
     * @brief Draw ESP boxes for all detections
     * @param result Detection results
     */
    void drawESPBoxes(const DetectionResult& result);
    
    /**
     * @brief Draw ImGui configuration menu
     */
    void drawMenu();
    
    /**
     * @brief Draw menu toggle button
     */
    void drawMenuButton();
    
    /**
     * @brief Draw touch zone overlay (moveable)
     */
    void drawTouchZoneOverlay();
    
    /**
     * @brief Draw info overlay (FPS, detection count)
     */
    void drawInfoOverlay(const DetectionResult& result);
    
    OverlayWindow* window_;
    bool initialized_;
    RenderConfig config_;
    mutable Timer timer_;
    
    // Menu button state
    bool menuButtonHovered_;
    
    // Box smoothing for temporal stability
    BoxSmoother boxSmoother_;
    std::array<BoundingBox, Config::MAX_DETECTIONS> smoothedBoxes_;
    int smoothedCount_{0};
};

} // namespace ESP

#endif // ESP_RENDERER_H
