#ifndef ESP_SETTINGS_H
#define ESP_SETTINGS_H

#include <cstdint>

/**
 * @file settings.h
 * @brief Centralized configuration constants for AimBuddy runtime
 * 
 * All configurable parameters are defined here to avoid magic numbers
 * and enable easy tuning without code changes.
 */

namespace Config {
    // ============================================================================
    // Screen Capture Configuration
    // ============================================================================
    
    /// Capture resolution width (1/3 of 1080p for SD888 optimization)
    constexpr int CAPTURE_WIDTH = 1280;
    
    /// Capture resolution height
    constexpr int CAPTURE_HEIGHT = 720;
    
    /// Center crop size for inference (square region)
    /// Reduced from 640 to 480 for better performance
    constexpr int CROP_SIZE = 480;
    
    /// ImageReader buffer depth (triple buffering to prevent producer stalls)
    constexpr int IMAGE_READER_MAX_IMAGES = 3;
    
    /// Frame capture interval in milliseconds
    constexpr int CAPTURE_INTERVAL_MS = 10;
    
    // ============================================================================
    // Model Configuration
    // ============================================================================
    
    /// YOLOv26n input size (optimized for 120 FPS target)
    constexpr int MODEL_INPUT_SIZE = 256;
    
    /// Model parameter file name
    constexpr const char* MODEL_PARAM_FILE = "models/yolo26n-opt.param";
    
    /// Model binary file name
    constexpr const char* MODEL_BIN_FILE = "models/yolo26n-opt.bin";
    
    /// Default confidence threshold for detections
    constexpr float DEFAULT_CONFIDENCE_THRESHOLD = 0.5f;
    
    /// NMS (Non-Maximum Suppression) IoU threshold
    /// YOLOv26n is NMS-free, so we use a standard threshold to merge overlapping boxes
    constexpr float NMS_IOU_THRESHOLD = 0.45f;
    
    /// Maximum number of detections per frame (pre-allocated)
    constexpr int MAX_DETECTIONS = 50;
    
    // ============================================================================
    // Detection Class Configuration
    // ============================================================================
    
    /// Number of classes in the model (single-class: enemy)
    constexpr int NUM_CLASSES = 1;
    
    /// Class ID for enemy (the only class we want to detect/highlight)
    constexpr int ENEMY_CLASS_ID = 0;
    
    /// Class ID for self (unused in single-class models)
    constexpr int SELF_CLASS_ID = 1;
    
    /// Class ID for teammate (unused in single-class models)
    constexpr int TEAMMATE_CLASS_ID = 2;
    
    /// Filter detections to only show enemies (class 0)
    /// Set to false to see all classes with different colors
    constexpr bool FILTER_ENEMY_ONLY = true;
    
    /// Only count/target enemies for aiming (even when showing all classes)
    constexpr bool TARGET_ENEMY_ONLY = true;
    
    // ============================================================================
    // NCNN Configuration (Adreno 660 Optimization)
    // ============================================================================
    
    /// Number of CPU threads for NCNN (fallback)
    constexpr int NCNN_NUM_THREADS = 4;
    
    /// Enable Vulkan compute shaders
    constexpr bool NCNN_USE_VULKAN_COMPUTE = true;
    
    /// Enable FP16 packed (critical for Adreno 660)
    constexpr bool NCNN_USE_FP16_PACKED = true;
    
    /// Enable FP16 storage (critical for Adreno 660)
    constexpr bool NCNN_USE_FP16_STORAGE = true;
    
    /// Enable FP16 arithmetic (Adreno 660 has native FP16 support)
    constexpr bool NCNN_USE_FP16_ARITHMETIC = true;
    
    /// Enable packing layout optimization
    constexpr bool NCNN_USE_PACKING_LAYOUT = true;
    
    /// Enable light mode (reduce memory footprint)
    constexpr bool NCNN_LIGHT_MODE = true;
    
    // ============================================================================
    // Threading Configuration
    // ============================================================================
    
    /// Ring buffer capacity (lock-free SPSC queue)
    /// Capacity 8 allows ~200ms of frame buffering at 40fps capture rate
    /// Needed when inference (40-45ms NCNN Vulkan) occasionally lags behind capture
    constexpr int RING_BUFFER_CAPACITY = 8;
    
    /// Inference thread CPU affinity (performance cores on SD888)
    constexpr int INFERENCE_THREAD_CPU_AFFINITY = 7;  // Cortex-X1 core
    
    /// Rendering thread CPU affinity
    constexpr int RENDERING_THREAD_CPU_AFFINITY = 6;  // Cortex-A78 core
    
    // ============================================================================
    // Rendering Configuration
    // ============================================================================
    
    /// Default ESP box color for ENEMY (RGB, 0-255) - Red for enemies
    constexpr uint8_t ENEMY_BOX_COLOR_R = 255;
    constexpr uint8_t ENEMY_BOX_COLOR_G = 0;
    constexpr uint8_t ENEMY_BOX_COLOR_B = 0;
    
    /// Default ESP box color for SELF (RGB, 0-255) - Blue for self
    constexpr uint8_t SELF_BOX_COLOR_R = 0;
    constexpr uint8_t SELF_BOX_COLOR_G = 0;
    constexpr uint8_t SELF_BOX_COLOR_B = 255;
    
    /// Default ESP box color for TEAMMATE (RGB, 0-255) - Green for teammates
    constexpr uint8_t TEAMMATE_BOX_COLOR_R = 0;
    constexpr uint8_t TEAMMATE_BOX_COLOR_G = 255;
    constexpr uint8_t TEAMMATE_BOX_COLOR_B = 0;
    
    /// Fallback/default box color
    constexpr uint8_t DEFAULT_BOX_COLOR_R = 255;
    constexpr uint8_t DEFAULT_BOX_COLOR_G = 0;
    constexpr uint8_t DEFAULT_BOX_COLOR_B = 0;
    
    /// Default box thickness in pixels (thicker for visibility)
    constexpr int DEFAULT_BOX_THICKNESS = 2;

    /// Default draw line setting
    constexpr bool DEFAULT_DRAW_LINE = false;

    /// Default draw dot setting
    constexpr bool DEFAULT_DRAW_DOT = false;
    
    /// Default detection FOV radius for dynamic crop detection (in pixels)
    /// Larger = more area scanned, slower. Smaller = faster, less coverage.
    constexpr float DEFAULT_DETECTION_FOV_RADIUS = 200.0f;

    /// Default aimbot target-selection FOV radius (in pixels)
    constexpr float DEFAULT_AIM_FOV_RADIUS = 175.0f;

    /// Legacy alias preserved for compatibility
    constexpr float DEFAULT_FOV_RADIUS = DEFAULT_DETECTION_FOV_RADIUS;
    
    /// Minimum box thickness
    constexpr int MIN_BOX_THICKNESS = 1;
    
    /// Maximum box thickness
    constexpr int MAX_BOX_THICKNESS = 5;
    
    /// Minimum confidence threshold for UI slider
    constexpr float MIN_CONFIDENCE_THRESHOLD = 0.3f;
    
    /// Maximum confidence threshold for UI slider
    constexpr float MAX_CONFIDENCE_THRESHOLD = 0.9f;
    
    /// ImGui menu toggle button size
    constexpr float MENU_BUTTON_SIZE = 60.0f;
    
    /// ImGui menu toggle button position (top-right corner offset)
    constexpr float MENU_BUTTON_OFFSET_X = 80.0f;
    constexpr float MENU_BUTTON_OFFSET_Y = 80.0f;
    
    // ============================================================================
    // Aimbot Configuration
    // ============================================================================
    
    /// Default Aimbot enabled state
    constexpr bool DEFAULT_AIMBOT_ENABLED = false; // Off by default for safety
    
    /// Default head offset ratio (0.0 = top, 0.5 = center, 1.0 = bottom)
    /// 0.2 puts it around the head/neck area for most humanoid targets
    constexpr float DEFAULT_HEAD_OFFSET = 0.2f;
    
    /// Default target lock duration in frames
    constexpr int DEFAULT_TARGET_LOCK_FRAMES = 3;
    
    /// Aimbot rate limiting (ms between injections) - prevents blocking user input
    constexpr int64_t AIMBOT_COOLDOWN_MS = 8; // 120Hz = ~8ms
    
    /// Aimbot deadzone in pixels - don't aim if target within this distance from center
    constexpr float AIMBOT_DEADZONE_PX = 5.0f;
    
    /// Minimum delta threshold - ignore movements smaller than this
    constexpr float AIMBOT_MIN_DELTA_PX = 1.0f;
    
    /// Aimbot smooth factor (0.0-1.0) - how much of the delta to apply per frame
    // Aimbot Settings
    constexpr float AIMBOT_MIN_SPEED = 0.15f;
    constexpr float AIMBOT_MAX_SPEED = 0.55f;
    constexpr float AIMBOT_SPEED_CURVE = 2.0f;
    
    constexpr float AIMBOT_FOV_X = 540.0f;
    constexpr float AIMBOT_FOV_Y = 540.0f;
    constexpr float AIMBOT_SNAP_RADIUS = 30.0f;
    constexpr float AIMBOT_NEAR_RADIUS = 200.0f;

    constexpr float AIMBOT_PREDICTION_INTERVAL = 0.2f;
    
    // ============================================================================
    
    /// Target inference latency in milliseconds
    constexpr int TARGET_INFERENCE_MS = 10;
    
    /// Target FPS for rendering
    constexpr int TARGET_FPS = 60;
    
    /// Maximum memory usage in MB
    constexpr int MAX_MEMORY_MB = 100;
    
    // ============================================================================
    // Logging Configuration
    // ============================================================================
    
    /// Log tag for Android logcat
    constexpr const char* LOG_TAG = "AimBuddy_Native";
}

// Logging policy:
// - Debug builds: full logging
// - Release builds: errors only
#if defined(NDEBUG)
    #define ENABLE_DEBUG_LOGGING 0
    #define ENABLE_INFO_LOGGING 0
    #define ENABLE_WARN_LOGGING 0
    #define ENABLE_ERROR_LOGGING 1
    #define ENABLE_PROFILING 0
#else
    #define ENABLE_DEBUG_LOGGING 1
    #define ENABLE_INFO_LOGGING 1
    #define ENABLE_WARN_LOGGING 1
    #define ENABLE_ERROR_LOGGING 1
    #define ENABLE_PROFILING 0
#endif

#endif // ESP_SETTINGS_H
