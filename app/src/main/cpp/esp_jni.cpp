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
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cmath>

// Fix for NDK compatibility issue usually caused by NCNN library mismatch
// Defines the missing symbol __libcpp_verbose_abort.
// We intentionally reopen the inline namespace here  -  suppress the Clang warning.
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
#include "capture/roi_capture.h"
#include "detector/yolo_detector.h"
#include "renderer/render_config.h"
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
    aimbuddy::RoiCapture g_roiCapture;
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

    // Cached JNI references
    JavaVM* g_jvm = nullptr;
    std::string g_modelParamPath;
    std::string g_modelBinPath;

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
     * Takes the newest region from the capture mailbox, runs the detector on it
     * and publishes the result. Frames that arrive while a detection is running
     * are replaced by newer ones, so the detector never works on stale pixels.
     */
    void inferenceLoop() {
        LOGI("=== Inference thread started ===");

        ESP::DetectionResult result;
        auto& mailbox = g_roiCapture.mailbox();

        constexpr uint64_t kStatsWindow = 120;
        uint64_t statsSamples = 0;
        double statsInferenceMs = 0.0;
        double statsEndToEndMs = 0.0;
        float appliedThreshold = -1.0f;

        while (g_running.load(std::memory_order_acquire)) {
            aimbuddy::RoiFrame* frame = mailbox.waitForLatest(std::chrono::milliseconds(50));
            if (frame == nullptr) {
                continue;
            }

            if (g_detector) {
                const float threshold = g_settings.confidenceThreshold;
                if (std::fabs(threshold - appliedThreshold) > 0.0001f) {
                    g_detector->setConfidenceThreshold(threshold);
                    appliedThreshold = threshold;
                }

                const auto inferStart = std::chrono::steady_clock::now();
                const bool detected = g_detector->detect(*frame, result);
                const auto inferEnd = std::chrono::steady_clock::now();

                if (detected) {
                    {
                        std::lock_guard<std::mutex> lock(g_resultMutex);
                        g_latestResult = result;
                    }
                    if (g_aimbot && g_settings.aimbotEnabled) {
                        g_aimbot->updateTargets(result.boxes.data(), result.boxes.size());
                    }

                    statsInferenceMs += std::chrono::duration<double, std::milli>(inferEnd - inferStart).count();
                    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        inferEnd.time_since_epoch()).count();
                    statsEndToEndMs += static_cast<double>(nowNs - frame->captureTimeNs) / 1'000'000.0;
                    if (++statsSamples >= kStatsWindow) {
                        LOGI("Pipeline: infer %.2f ms, frame age %.2f ms, replaced frames %llu",
                             statsInferenceMs / static_cast<double>(statsSamples),
                             statsEndToEndMs / static_cast<double>(statsSamples),
                             static_cast<unsigned long long>(mailbox.overwrittenCount()));
                        statsSamples = 0;
                        statsInferenceMs = 0.0;
                        statsEndToEndMs = 0.0;
                    }
                }
            }
            mailbox.endRead();
        }

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
    g_roiCapture.setScreenSize(width, height);

    g_settings.screenWidth = width;
    g_settings.screenHeight = height;
    g_settings.validate();
    SyncUnifiedSettingsToRenderConfig();

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

/**
 * @brief Detection zone as a rectangle in screen pixels, for the overlay to outline
 * @return false until capture has been configured
 */
bool GetDetectionZoneScreenRect(float* left, float* top, float* right, float* bottom) {
    const aimbuddy::CaptureGeometry geometry = g_roiCapture.geometry();
    const aimbuddy::RoiRect roi = g_roiCapture.currentRoi();
    if (!geometry.isValid() || roi.side <= 0 || !left || !top || !right || !bottom) {
        return false;
    }
    const float scale = geometry.screenPerCapture();
    *left = static_cast<float>(roi.x) * scale;
    *top = static_cast<float>(roi.y) * scale;
    *right = static_cast<float>(roi.x + roi.side) * scale;
    *bottom = static_cast<float>(roi.y + roi.side) * scale;
    return true;
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
    const char* modelParamPath = g_modelParamPath.empty() ? nullptr : g_modelParamPath.c_str();
    const char* modelBinPath = g_modelBinPath.empty() ? nullptr : g_modelBinPath.c_str();
    if (!g_detector->initialize(mgr, modelParamPath, modelBinPath)) {
        LOGE("Failed to initialize detector");
        g_detector.reset();
        return JNI_FALSE;
    }

    // Create TouchHelper and AimbotController (but DON'T init touch yet)
    // Touch init happens in nativeInitAimbot() AFTER root permissions are set
    g_touchHelper = std::make_unique<TouchHelper>();
    g_touchHelper->setJniBridge(g_jvm, env, thiz);
    g_touchHelper->setBackend(g_settings.touchBackend == 1 ? TouchBackend::SHIZUKU : TouchBackend::UINPUT);
    g_touchHelper->setScreenSize(screenWidth, screenHeight);

    // Create aimbot controller with touch helper
    // NOTE: We don't call init() or start() here - that happens after root
    g_aimbot = std::make_unique<AimbotController>(g_touchHelper.get(), screenWidth, screenHeight);

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

    if (!g_detector) {
        LOGE("Cannot start: components not initialized");
        return;
    }

    // Start inference thread
    g_roiCapture.mailbox().reopen();
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
    g_roiCapture.mailbox().close();

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
    g_roiCapture.mailbox().close();
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
    g_detector.reset();

    LOGI("Native shutdown complete");
}

/**
 * @brief Handle incoming frame from screen capture
 * @param hardwareBuffer AHardwareBuffer from ImageReader; only valid during this call
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_ScreenCaptureService_nativeOnFrame(JNIEnv* env, jclass clazz,
                                                 jobject hardwareBuffer,
                                                 jlong timestamp) {
    (void)clazz;
    if (!g_running.load(std::memory_order_acquire) || !hardwareBuffer) {
        return;
    }

    AHardwareBuffer* buffer = AHardwareBuffer_fromHardwareBuffer(env, hardwareBuffer);
    if (!buffer) {
        LOGW("Failed to get AHardwareBuffer");
        return;
    }

    // The region is copied before this call returns, so the buffer can be recycled right away.
    g_roiCapture.setZoneRadius(g_settings.fovRadius);
    g_roiCapture.ingest(buffer, static_cast<int64_t>(timestamp));
}

/**
 * @brief Tell the native side how large the screen and the capture image are
 * @param screenWidth Overlay width in pixels (landscape)
 * @param screenHeight Overlay height in pixels (landscape)
 * @param captureWidth ImageReader width in pixels
 * @param captureHeight ImageReader height in pixels
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeConfigureCapture(JNIEnv* env, jobject thiz,
                                                   jint screenWidth, jint screenHeight,
                                                   jint captureWidth, jint captureHeight) {
    (void)env;
    (void)thiz;
    aimbuddy::CaptureGeometry geometry;
    geometry.screenWidth = screenWidth;
    geometry.screenHeight = screenHeight;
    geometry.captureWidth = captureWidth;
    geometry.captureHeight = captureHeight;
    g_roiCapture.configure(geometry);
    LOGI("Capture configured: screen %dx%d, capture %dx%d", screenWidth, screenHeight,
         captureWidth, captureHeight);
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
    }

    g_touchHelper->setJniBridge(g_jvm, env, thiz);
    g_touchHelper->setBackend(g_settings.touchBackend == 1 ? TouchBackend::SHIZUKU : TouchBackend::UINPUT);
    g_touchHelper->setScreenSize(g_screenWidth, g_screenHeight);

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

    LOGE("TouchHelper init FAILED for backend=%d", g_settings.touchBackend);
    LOGE("Check: uinput requires root, Shizuku requires active service + permission");
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeSetTouchBackend(JNIEnv* /* env */, jobject /* thiz */, jint backend) {
    const int clamped = std::max(0, std::min(1, static_cast<int>(backend)));
    g_settings.touchBackend = clamped;
    g_settings.validate();

    if (g_touchHelper) {
        g_touchHelper->setBackend(clamped == 1 ? TouchBackend::SHIZUKU : TouchBackend::UINPUT);
    }
    LOGI("Touch backend set to %d", clamped);
}

JNIEXPORT jint JNICALL
Java_com_aimbuddy_MainActivity_nativeGetTouchBackend(JNIEnv* /* env */, jobject /* thiz */) {
    return g_settings.touchBackend;
}

/**
 * Push streamer-mode (FLAG_SECURE) state to MainActivity via JNI.
 * Called from imgui_menu.cpp whenever the user toggles the setting.
 */
void NotifyStreamerModeChanged(bool enabled) {
    if (!g_jvm) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        attached = true;
    }
    jclass cls = env->FindClass("com/aimbuddy/MainActivity");
    if (cls) {
        jmethodID method = env->GetStaticMethodID(cls, "nativeApplyStreamerMode", "(Z)V");
        if (method) {
            env->CallStaticVoidMethod(cls, method, static_cast<jboolean>(enabled));
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(cls);
    }
    if (attached) g_jvm->DetachCurrentThread();
}

JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeSetShizukuBridgeAvailable(JNIEnv* /* env */, jobject /* thiz */, jboolean available) {
    if (g_touchHelper) {
        g_touchHelper->setShizukuBridgeAvailable(available == JNI_TRUE);
    }
}

JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeSetModelPaths(JNIEnv* env, jobject thiz,
                                                   jstring paramPath,
                                                   jstring binPath) {
    (void)thiz;

    g_modelParamPath.clear();
    g_modelBinPath.clear();

    if (paramPath != nullptr) {
        const char* chars = env->GetStringUTFChars(paramPath, nullptr);
        if (chars != nullptr) {
            g_modelParamPath.assign(chars);
            env->ReleaseStringUTFChars(paramPath, chars);
        }
    }

    if (binPath != nullptr) {
        const char* chars = env->GetStringUTFChars(binPath, nullptr);
        if (chars != nullptr) {
            g_modelBinPath.assign(chars);
            env->ReleaseStringUTFChars(binPath, chars);
        }
    }

    LOGI("Updated model paths: param='%s' bin='%s'",
         g_modelParamPath.c_str(),
         g_modelBinPath.c_str());
}

} // extern "C"
