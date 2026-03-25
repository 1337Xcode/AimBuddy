#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cmath>

// Fix for NDK compatibility issue usually caused by NCNN library mismatch
// Defines the missing symbol __libcpp_verbose_abort.
// We intentionally reopen the inline namespace here — suppress the Clang warning.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winline-namespace-reopened-noninline"
namespace std {
    namespace __ndk1 {
        void __libcpp_verbose_abort(const char* format, ...) {
            va_list args;
            va_start(args, format);
            vfprintf(stderr, format, args);
            va_end(args);
            abort();
        }
    }
}
#pragma clang diagnostic pop

#include "settings.h"
#include "utils/logger.h"
#include "utils/thread.h"
#include "utils/timer.h"
#include "capture/frame_buffer.h"
#include "detector/yolo_detector.h"
#include "renderer/esp_renderer.h"
#include "aimbot/aimbot_controller.h"

/**
 * @file esp_jni.cpp
 * @brief JNI entry point and thread orchestration
 * 
 * Manages the lifecycle of detector, renderer, and worker threads.
 * Provides JNI bindings for Kotlin code.
 * Coordinates frame ingestion, inference, and rendering handoff.
 */

#include <mutex>
#include "input/touch_helper.h"
#include "utils/aimbot_types.h"

// Global unified settings (ALL settings in one place)
UnifiedSettings g_settings;

// Shared render configuration (legacy - now part of g_settings)
ESP::RenderConfig g_renderConfig;

namespace {

    // Global state (managed via RAII)
    std::unique_ptr<ESP::YoloDetector> g_detector;
    std::unique_ptr<ESP::FrameBuffer> g_frameBuffer;
    std::unique_ptr<AimbotController> g_aimbot;
    std::unique_ptr<TouchHelper> g_touchHelper;
    
    // Threading
    std::unique_ptr<ESP::Thread> g_inferenceThread;
    std::atomic<bool> g_running{false};
    
    // Latest detection result (shared between inference and render threads)
    ESP::DetectionResult g_latestResult;
    std::mutex g_resultMutex;
    
    // Screen dimensions
    int g_screenWidth = 1080;
    int g_screenHeight = 2400;

    // Capture dimensions (match ImageReader config)
    int g_captureWidth = Config::CAPTURE_WIDTH;
    int g_captureHeight = Config::CAPTURE_HEIGHT;
    
    // Cached JNI references
    JavaVM* g_jvm = nullptr;

    void SyncUnifiedSettingsToRenderConfig() {
        g_renderConfig.boxColorR.store(g_settings.boxColorR, std::memory_order_relaxed);
        g_renderConfig.boxColorG.store(g_settings.boxColorG, std::memory_order_relaxed);
        g_renderConfig.boxColorB.store(g_settings.boxColorB, std::memory_order_relaxed);
        g_renderConfig.boxThickness.store(g_settings.boxThickness, std::memory_order_relaxed);
        g_renderConfig.confidenceThreshold.store(g_settings.confidenceThreshold, std::memory_order_relaxed);
        g_renderConfig.fovRadius.store(g_settings.fovRadius, std::memory_order_relaxed);
        g_renderConfig.showFPS.store(g_settings.showFPS, std::memory_order_relaxed);
        g_renderConfig.showDetectionCount.store(g_settings.showDetectionCount, std::memory_order_relaxed);
        g_renderConfig.showLabels.store(g_settings.showLabels, std::memory_order_relaxed);
        g_renderConfig.drawLine.store(g_settings.drawLine, std::memory_order_relaxed);
        g_renderConfig.drawDot.store(g_settings.drawDot, std::memory_order_relaxed);
        g_renderConfig.enableSmoothing.store(g_settings.enableSmoothing, std::memory_order_relaxed);
        g_renderConfig.smoothingFactor.store(g_settings.smoothingFactor, std::memory_order_relaxed);

        g_renderConfig.aimbotEnabled.store(g_settings.aimbotEnabled, std::memory_order_relaxed);
        g_renderConfig.headOffset.store(g_settings.headOffset, std::memory_order_relaxed);

        if (g_settings.screenWidth > 0 && g_settings.screenHeight > 0) {
            float centerX = g_settings.touchX / static_cast<float>(g_settings.screenWidth);
            float centerY = g_settings.touchY / static_cast<float>(g_settings.screenHeight);
            g_renderConfig.touchCenterX.store(centerX, std::memory_order_relaxed);
            g_renderConfig.touchCenterY.store(centerY, std::memory_order_relaxed);
        }
        g_renderConfig.touchRadius.store(g_settings.touchRadius, std::memory_order_relaxed);
        g_renderConfig.aimDelay.store(g_settings.aimDelay, std::memory_order_relaxed);
    }
    
    /**
     * @brief Inference thread main loop
     * 
     * Consumes frames from ring buffer, runs YOLO inference,
     * and updates shared detection result.
     */
    void inferenceLoop() {
        LOGI("=== Inference thread started ===");
        
        // Attach thread to JVM for JNI calls
        JNIEnv* env = nullptr;
        if (g_jvm->AttachCurrentThread(&env, nullptr) != 0) {
            LOGE("Failed to attach inference thread to JVM");
            return;
        }
        
        ESP::Frame frame;
        ESP::DetectionResult result;
        float cachedThreshold = -1.0f;
        float cachedFovRadius = -1.0f;
        int cachedCropSize = Config::CROP_SIZE;

        uint64_t statsSamples = 0;
        [[maybe_unused]] uint64_t statsDrainedFrames = 0;  // accumulated in window, reset at report
        double statsInferenceMs = 0.0;
        double statsEndToEndMs = 0.0;
        uint64_t statsEndToEndSamples = 0;
        constexpr uint64_t kStatsWindow = 120;
        constexpr double kTargetCycleMs = 14.0;
        constexpr double kE2ePressureMs = 20.0;
        constexpr double kEmaAlpha = 0.15;
        constexpr int kMinAdaptiveCrop = 224;
        constexpr int kDownscaleStep = 16;
        constexpr int kUpscaleStep = 8;
        constexpr auto kNoFrameSleepMin = std::chrono::microseconds(200);
        constexpr auto kNoFrameSleepMax = std::chrono::microseconds(2000);

        double emaInferMs = 0.0;
        double emaEndToEndMs = 0.0;
        uint32_t noFrameBackoffLevel = 0;
        int adaptiveCropSize = cachedCropSize;
        
        while (g_running.load(std::memory_order_acquire)) {
            // Try to get a frame from buffer
            if (g_frameBuffer && g_frameBuffer->pop(frame)) {
                noFrameBackoffLevel = 0;

                // Drain to latest frame to reduce latency
                ESP::Frame newer;
                uint64_t drainedThisIteration = 0;
                while (g_frameBuffer->pop(newer)) {
                    if (frame.hardwareBuffer) {
                        AHardwareBuffer_release(frame.hardwareBuffer);
                    }
                    frame = newer;
                    drainedThisIteration++;
                }
                statsDrainedFrames += drainedThisIteration;

                if (frame.hardwareBuffer && g_detector) {
                    // Load settings once
                    const float threshold = g_settings.confidenceThreshold;
                    const float fovRadius = g_settings.fovRadius;
                    
                    // Update detector threshold only when changed
                    if (std::fabs(threshold - cachedThreshold) > 0.0001f) {
                        g_detector->setConfidenceThreshold(threshold);
                        cachedThreshold = threshold;
                    }

                    // Recompute dynamic crop only when fov changes
                    if (std::fabs(fovRadius - cachedFovRadius) > 0.0001f) {
                        const int safeScreenWidth = std::max(1, g_screenWidth);
                        int targetSize = static_cast<int>(fovRadius * 2.0f);
                        targetSize = std::max(256, std::min(targetSize, safeScreenWidth));

                        const float scaleToCapture = static_cast<float>(Config::CAPTURE_WIDTH) / static_cast<float>(safeScreenWidth);
                        int dynamicCropSize = static_cast<int>(targetSize * scaleToCapture);
                        dynamicCropSize = std::max(256, std::min(dynamicCropSize, Config::CROP_SIZE));

                        cachedCropSize = dynamicCropSize;
                        adaptiveCropSize = cachedCropSize;
                        cachedFovRadius = fovRadius;
                    }

                    const auto inferStart = std::chrono::steady_clock::now();
                    
                    // Run detection with dynamic crop
                    if (g_detector->detect(frame.hardwareBuffer, result, adaptiveCropSize)) {
                        const auto inferEnd = std::chrono::steady_clock::now();
                        const double inferMs = std::chrono::duration<double, std::milli>(inferEnd - inferStart).count();
                        statsInferenceMs += inferMs;
                        statsSamples++;

                        if (emaInferMs <= 0.0) {
                            emaInferMs = inferMs;
                        } else {
                            emaInferMs += (inferMs - emaInferMs) * kEmaAlpha;
                        }

                        bool hasEndToEnd = false;
                        double e2eMs = 0.0;

                        if (frame.timestamp > 0) {
                            const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count();
                            if (nowNs > frame.timestamp) {
                                e2eMs = static_cast<double>(nowNs - frame.timestamp) / 1'000'000.0;
                                if (e2eMs >= 0.0 && e2eMs < 2000.0) {
                                    statsEndToEndMs += e2eMs;
                                    statsEndToEndSamples++;
                                    hasEndToEnd = true;
                                }
                            }
                        }

                        if (hasEndToEnd) {
                            if (emaEndToEndMs <= 0.0) {
                                emaEndToEndMs = e2eMs;
                            } else {
                                emaEndToEndMs += (e2eMs - emaEndToEndMs) * kEmaAlpha;
                            }
                        }

                        const bool backlogPressure = (drainedThisIteration > 0);
                        const bool latencyPressure = (emaInferMs > kTargetCycleMs) || (emaEndToEndMs > kE2ePressureMs);

                        if (latencyPressure || backlogPressure) {
                            adaptiveCropSize = std::max(kMinAdaptiveCrop, adaptiveCropSize - kDownscaleStep);
                        } else if (adaptiveCropSize < cachedCropSize) {
                            adaptiveCropSize = std::min(cachedCropSize, adaptiveCropSize + kUpscaleStep);
                        }

                        // Copy result to shared state (Thread-Safe)
                        {
                            std::lock_guard<std::mutex> lock(g_resultMutex);
                            g_latestResult = result;
                        }

                        // Update Aimbot Target Logic (Thread-Safe)
                        if (g_aimbot && g_settings.aimbotEnabled) {
                             g_aimbot->updateTargets(result.boxes.data(), result.boxes.size());
                        }

                        if (statsSamples >= kStatsWindow) {
                            const uint32_t droppedAtPush = g_frameBuffer ? g_frameBuffer->consumeDroppedFrameCount() : 0;
                            const double avgInfer = statsInferenceMs / static_cast<double>(statsSamples);
                            const double avgEndToEnd = (statsEndToEndSamples > 0)
                                ? (statsEndToEndMs / static_cast<double>(statsEndToEndSamples))
                                : 0.0;

                            LOGI("Pipeline stats: avg infer=%.2fms avg e2e=%.2fms ema infer=%.2fms ema e2e=%.2fms crop=%d drained=%llu dropped_push=%u",
                                 avgInfer,
                                 avgEndToEnd,
                                 emaInferMs,
                                 emaEndToEndMs,
                                 adaptiveCropSize,
                                 static_cast<unsigned long long>(statsDrainedFrames),
                                 droppedAtPush);

                            statsSamples = 0;
                            statsDrainedFrames = 0;
                            statsInferenceMs = 0.0;
                            statsEndToEndMs = 0.0;
                            statsEndToEndSamples = 0;
                        }

                        if (!backlogPressure && inferMs < kTargetCycleMs) {
                            const double slackMs = kTargetCycleMs - inferMs;
                            if (slackMs > 0.15) {
                                std::this_thread::sleep_for(
                                    std::chrono::microseconds(static_cast<int64_t>(slackMs * 1000.0))
                                );
                            }
                        }
                    }
                }

                if (frame.hardwareBuffer) {
                    AHardwareBuffer_release(frame.hardwareBuffer);
                }
            } else {
                // No frame available, sleep briefly
                const auto sleepDuration = std::min(kNoFrameSleepMin * (1u << noFrameBackoffLevel), kNoFrameSleepMax);
                std::this_thread::sleep_for(sleepDuration);
                if (noFrameBackoffLevel < 4) {
                    ++noFrameBackoffLevel;
                }
            }
        }
        
        g_jvm->DetachCurrentThread();
        LOGI("Inference thread stopped");
    }
}

extern "C" {

// Accessors for GLSurfaceView renderer (imgui_menu.cpp)
ESP::RenderConfig* GetRenderConfig() {
    return &g_renderConfig;
}

AimbotController* GetAimbotController() {
    return g_aimbot.get();
}

void UpdateScreenSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    g_screenWidth = width;
    g_screenHeight = height;

    g_settings.screenWidth = width;
    g_settings.screenHeight = height;
    g_settings.validate();
    SyncUnifiedSettingsToRenderConfig();

    if (g_detector) {
        g_detector->setScreenSize(width, height);
    }
    if (g_touchHelper) {
        g_touchHelper->setScreenSize(width, height);
    }
    if (g_aimbot) {
        g_aimbot->setScreenSize(width, height);
    }
}

bool GetLatestResultSnapshot(ESP::DetectionResult* out) {
    if (!out) {
        return false;
    }
    // Copy the most recent result (thread-safe copy)
    std::lock_guard<std::mutex> lock(g_resultMutex);
    *out = g_latestResult;
    return true;
}

void GetCaptureSize(int* outWidth, int* outHeight) {
    if (outWidth) *outWidth = g_captureWidth;
    if (outHeight) *outHeight = g_captureHeight;
}

/**
 * @brief JNI_OnLoad - Called when native library is loaded
 */
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("Native library loaded");
    g_jvm = vm;
    
    // Initialize NCNN for Vulkan
    ncnn::create_gpu_instance();
    
    return JNI_VERSION_1_6;
}

/**
 * @brief JNI_OnUnload - Called when native library is unloaded
 */
JNIEXPORT void JNI_OnUnload(JavaVM* vm, void* reserved) {
    LOGI("Native library unloading");
    ncnn::destroy_gpu_instance();
}

/**
 * @brief Initialize native components
 */
JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_MainActivity_nativeInit(JNIEnv* env, jobject thiz,
                                      jobject assetManager,
                                      jint screenWidth, jint screenHeight) {
    LOGI("nativeInit: screen %dx%d", screenWidth, screenHeight);
    
    g_screenWidth = screenWidth;
    g_screenHeight = screenHeight;
    
    // Load unified settings from disk
    if (g_settings.load()) {
        LOGI("Loaded settings from disk");
        g_settings.validate();
    } else {
        LOGI("Using default settings");
        g_settings.setDefaultTouchPosition(screenWidth, screenHeight);
    }
    
    // Set runtime values
    g_settings.screenWidth = screenWidth;
    g_settings.screenHeight = screenHeight;
    SyncUnifiedSettingsToRenderConfig();
    
    // Get native asset manager
    AAssetManager* mgr = AAssetManager_fromJava(env, assetManager);
    if (!mgr) {
        LOGE("Failed to get AssetManager");
        return JNI_FALSE;
    }
    
    // Create and initialize detector
    g_detector = std::make_unique<ESP::YoloDetector>();
    if (!g_detector->initialize(mgr, screenWidth, screenHeight)) {
        LOGE("Failed to initialize detector");
        g_detector.reset();
        return JNI_FALSE;
    }
    
    // Create TouchHelper and AimbotController (but DON'T init touch yet)
    // Touch init happens in nativeInitAimbot() AFTER root permissions are set
    g_touchHelper = std::make_unique<TouchHelper>();
    g_touchHelper->setScreenSize(screenWidth, screenHeight);
    
    // Create aimbot controller with touch helper
    // NOTE: We don't call init() or start() here - that happens after root
    g_aimbot = std::make_unique<AimbotController>(g_touchHelper.get(), screenWidth, screenHeight);
    
    // Create frame buffer
    g_frameBuffer = std::make_unique<ESP::FrameBuffer>();
    
    LOGI("Native components initialized");
    return JNI_TRUE;
}

/**
 * @brief Start inference thread
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeStart(JNIEnv* env, jobject thiz) {
    LOGI("nativeStart called");
    
    if (g_running.load(std::memory_order_acquire)) {
        LOGI("Inference already running");
        return;
    }
    
    if (!g_detector || !g_frameBuffer) {
        LOGE("Cannot start: components not initialized");
        return;
    }
    
    // Start inference thread
    g_running.store(true, std::memory_order_release);
    g_inferenceThread = std::make_unique<ESP::Thread>(inferenceLoop, "InferenceThread");
    
    if (!g_inferenceThread->start(Config::INFERENCE_THREAD_CPU_AFFINITY)) {
        LOGE("Failed to start inference thread!");
        g_running.store(false, std::memory_order_release);
        g_inferenceThread.reset();
        return;
    }
    
    LOGI("Inference thread started successfully");
}

/**
 * @brief Stop inference thread
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeStop(JNIEnv* env, jobject thiz) {
    LOGI("nativeStop called");
    
    if (!g_running.load(std::memory_order_acquire)) {
        LOGI("Inference not running");
        return;
    }
    
    // Stop inference thread
    g_running.store(false, std::memory_order_release);
    
    if (g_inferenceThread) {
        g_inferenceThread->join();
        g_inferenceThread.reset();
    }
    
    LOGI("Inference thread stopped");
}

/**
 * @brief Shutdown and cleanup all native resources
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeShutdown(JNIEnv* env, jobject thiz) {
    LOGI("Shutting down native components");
    
    // Save unified settings to disk
    if (g_settings.save()) {
        LOGI("Settings saved successfully");
    } else {
        LOGE("Failed to save settings");
    }
    
    // Stop if running
    g_running.store(false, std::memory_order_release);
    if (g_inferenceThread) {
        g_inferenceThread->join();
        g_inferenceThread.reset();
    }
    
    // Stop Aimbot Thread
    if (g_aimbot) {
        g_aimbot->stop();
        g_aimbot.reset();
    }
    
    if (g_touchHelper) {
        g_touchHelper->shutdown();
        g_touchHelper.reset();
    }
    
    // Cleanup 
    g_frameBuffer.reset();
    g_detector.reset();
    
    LOGI("Native shutdown complete");
}

/**
 * @brief Handle incoming frame from screen capture
 * @param hardwareBuffer AHardwareBuffer from ImageReader
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_ScreenCaptureService_nativeOnFrame(JNIEnv* env, jclass clazz,
                                                 jobject hardwareBuffer,
                                                 jlong timestamp) {
    if (!g_running.load(std::memory_order_acquire)) {
        return;
    }
    
    if (!hardwareBuffer || !g_frameBuffer) {
        return;
    }
    
    // Get native hardware buffer
    AHardwareBuffer* buffer = AHardwareBuffer_fromHardwareBuffer(env, hardwareBuffer);
    if (!buffer) {
        LOGW("Failed to get AHardwareBuffer");
        return;
    }
    
    // Acquire reference (will be released after inference)
    AHardwareBuffer_acquire(buffer);
    
    // Push to frame buffer
    ESP::Frame frame;
    frame.hardwareBuffer = buffer;
    frame.timestamp = timestamp;
    frame.width = g_captureWidth;
    frame.height = g_captureHeight;
    
    if (!g_frameBuffer->push(frame)) {
        // Buffer full - release; drop count is tracked in FrameBuffer for periodic telemetry
        AHardwareBuffer_release(buffer);
    }
}

/**
 * @brief Render one frame (called from render thread)
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeRender(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    // Rendering handled by GLSurfaceView (imgui_menu.cpp)
}

/**
 * @brief Handle touch event
 * @param action Touch action (0=down, 1=up, 2=move)
 * @param x X coordinate
 * @param y Y coordinate
 * @return true if event was consumed by ImGui
 */
JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_MainActivity_nativeOnTouch(JNIEnv* env, jobject thiz,
                                         jint action, jfloat x, jfloat y) {
    (void)env;
    (void)thiz;
    (void)action;
    (void)x;
    (void)y;
    return JNI_FALSE;
}

/**
 * @brief Get current FPS
 */
JNIEXPORT jfloat JNICALL
Java_com_aimbuddy_MainActivity_nativeGetFPS(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    return 0.0f;
}

/**
 * @brief Check if ESP is running
 */
JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_MainActivity_nativeIsRunning(JNIEnv* env, jobject thiz) {
    return g_running.load(std::memory_order_acquire) ? JNI_TRUE : JNI_FALSE;
}

/**
 * @brief Initialize/Re-initialize aimbot components (called after root grant)
 * This is where TouchHelper.init() actually happens - AFTER root permissions
 */
JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_MainActivity_nativeInitAimbot(JNIEnv* env, jobject thiz) {
    LOGI("nativeInitAimbot called - initializing TouchHelper with root");
    
    if (!g_touchHelper) {
        LOGE("TouchHelper is null, creating new one");
        g_touchHelper = std::make_unique<TouchHelper>();
        g_touchHelper->setScreenSize(g_screenWidth, g_screenHeight);
    }
    
    // THIS is where we actually init the touch device (needs root)
    if (g_touchHelper->init()) {
        LOGI("TouchHelper initialized successfully!");
        LOGI("Touch device opened, uinput created, grab active");
        
        // Now start aimbot controller since touch is working
        if (g_aimbot) {
            g_aimbot->start();
            LOGI("AimbotController started with working touch");
        } else {
            g_aimbot = std::make_unique<AimbotController>(g_touchHelper.get(), g_screenWidth, g_screenHeight);
            g_aimbot->start();
            LOGI("AimbotController created and started");
        }
        return JNI_TRUE;
    }
    
    LOGE("TouchHelper init FAILED!");
    LOGE("Check: 1) Root granted  2) /dev/uinput exists  3) /dev/input/event* accessible");
    return JNI_FALSE;
}

} // extern "C"
