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
#include "pipeline/detection_pipeline.h"
#include "pipeline/pipeline_access.h"
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
    aimbuddy::DetectionPipeline g_pipeline;
    aimbuddy::ModelProfile g_modelProfile;
    std::unique_ptr<AimbotController> g_aimbot;
    std::unique_ptr<TouchHelper> g_touchHelper;

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
     * Bridge to the aim controller, which still tracks targets on its own.
     * Hands it the confident body detections of each batch.
     */
    void feedAimbot(const aimbuddy::DetectionBatch& batch) {
        if (!g_aimbot || !g_settings.aimbotEnabled) {
            return;
        }
        ESP::BoundingBox boxes[aimbuddy::kMaxDetections];
        int count = 0;
        for (int i = 0; i < batch.count; ++i) {
            const aimbuddy::Detection& d = batch.detections[i];
            if (g_pipeline.profile().roleOf(d.classId) != aimbuddy::ClassRole::kBody ||
                d.score < g_settings.confidenceThreshold) {
                continue;
            }
            boxes[count++] = ESP::BoundingBox(d.x, d.y, d.width, d.height, d.score, Config::ENEMY_CLASS_ID);
        }
        g_aimbot->updateTargets(boxes, count);
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

/**
 * @brief Detection zone as a rectangle in screen pixels, for the overlay to outline
 * @return false until capture has been configured
 */
bool GetDetectionZoneScreenRect(float* left, float* top, float* right, float* bottom) {
    const aimbuddy::CaptureGeometry geometry = g_pipeline.capture().geometry();
    const aimbuddy::RoiRect roi = g_pipeline.capture().currentRoi();
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

    // Load the model. The paths are empty when the bundled model should be used.
    if (!g_pipeline.loadModel(mgr, g_modelParamPath, g_modelBinPath, g_modelProfile)) {
        LOGE("Failed to load the detection model");
        return JNI_FALSE;
    }
    g_pipeline.setObserver(feedAimbot);
    g_pipeline.setConfidenceThreshold(g_settings.confidenceThreshold);

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
 * @brief Start the detection pipeline
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeStart(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    LOGI("nativeStart called");
    if (!g_pipeline.isModelLoaded()) {
        LOGE("Cannot start: no model loaded");
        return;
    }
    g_pipeline.start();
}

/**
 * @brief Stop the detection pipeline
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeStop(JNIEnv* env, jobject thiz) {
    (void)env;
    (void)thiz;
    LOGI("nativeStop called");
    g_pipeline.stop();
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

    // Stop the pipeline first: it feeds the aim controller
    g_pipeline.stop();

    // Stop Aimbot Thread
    if (g_aimbot) {
        g_aimbot->stop();
        g_aimbot.reset();
    }

    if (g_touchHelper) {
        g_touchHelper->shutdown();
        g_touchHelper.reset();
    }

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
    if (!g_pipeline.isRunning() || !hardwareBuffer) {
        return;
    }

    AHardwareBuffer* buffer = AHardwareBuffer_fromHardwareBuffer(env, hardwareBuffer);
    if (!buffer) {
        LOGW("Failed to get AHardwareBuffer");
        return;
    }

    // The region is copied before this call returns, so the buffer can be recycled right away.
    g_pipeline.capture().setZoneRadius(g_settings.fovRadius);
    g_pipeline.capture().ingest(buffer, static_cast<int64_t>(timestamp));
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
    g_pipeline.capture().configure(geometry);
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
    return g_pipeline.isRunning() ? JNI_TRUE : JNI_FALSE;
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

/**
 * @brief Tell the detector what the model's classes mean
 * @param roles Comma separated roles in class order, such as "body,head"; null or empty when unknown
 * @param endToEnd True if the model already removes duplicate detections
 * @param inputSize Width and height of the network input, or 0 to work it out
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_MainActivity_nativeSetModelProfile(JNIEnv* env, jobject thiz, jstring roles,
                                                  jboolean endToEnd, jint inputSize) {
    (void)thiz;
    std::string list;
    if (roles != nullptr) {
        const char* chars = env->GetStringUTFChars(roles, nullptr);
        if (chars != nullptr) {
            list.assign(chars);
            env->ReleaseStringUTFChars(roles, chars);
        }
    }
    g_modelProfile = list.empty() ? aimbuddy::ModelProfile() : aimbuddy::ModelProfile::parseRoles(list, 0);
    g_modelProfile.endToEndOutput = endToEnd == JNI_TRUE;
    g_modelProfile.inputSize = inputSize > 0 ? inputSize : 0;
    LOGI("Model profile: roles='%s' endToEnd=%d inputSize=%d", list.c_str(), endToEnd == JNI_TRUE, inputSize);
}

} // extern "C"

aimbuddy::DetectionPipeline& aimbuddy::pipeline() { return g_pipeline; }
