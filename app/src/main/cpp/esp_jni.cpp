#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>

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

#include "app_state.h"
#include "utils/logger.h"

/**
 * @file esp_jni.cpp
 * @brief The native side of the app's Kotlin code.
 *
 * Creates the app's long-lived parts (detection pipeline, settings, touch link, aim loop) and
 * exposes them to Kotlin through NativeBridge. Nothing here decides anything: it passes
 * requests to the part that owns the decision.
 */

extern "C" bool IsZoneEditorOpen();   // renderer/imgui_menu.cpp

namespace {

using aimbuddy::Settings;

aimbuddy::DetectionPipeline g_pipeline;
aimbuddy::SettingsStore g_settingsStore;
aimbuddy::touch::TouchLink g_touchLink;
aimbuddy::CameraMotion g_cameraMotion;
aimbuddy::AimLoop g_aimLoop(&g_pipeline.tracker(), &g_pipeline.profile(), &g_touchLink, &g_cameraMotion);

JavaVM* g_jvm = nullptr;
std::string g_settingsPath;
bool g_settingsLoaded = false;
bool g_modelLoaded = false;

/// Copies a Java string, or returns an empty one for null.
std::string toStdString(JNIEnv* env, jstring text) {
    if (text == nullptr) {
        return std::string();
    }
    const char* chars = env->GetStringUTFChars(text, nullptr);
    if (chars == nullptr) {
        return std::string();
    }
    std::string copy(chars);
    env->ReleaseStringUTFChars(text, chars);
    return copy;
}

/// Turns the player's settings into what the aim loop works from.
aimbuddy::AimLoopConfig makeAimLoopConfig() {
    const Settings settings = g_settingsStore.get();
    const aimbuddy::CaptureGeometry geometry = g_pipeline.capture().geometry();
    const float displayWidth = geometry.isValid() ? static_cast<float>(geometry.screenWidth) : 0.0f;
    const float displayHeight = geometry.isValid() ? static_cast<float>(geometry.screenHeight) : 0.0f;

    aimbuddy::AimLoopConfig config;
    // While the player moves the touch area on screen, the assist stays out of the way.
    config.enabled = settings.assistEnabled && !IsZoneEditorOpen();
    config.aim.zone = static_cast<aimbuddy::AimZone>(settings.aimZone);
    config.aim.strength = settings.aimStrength;
    config.aim.fovRadiusPx = settings.aimFovRadius;
    config.aim.priority = settings.aimPriority;
    config.aim.anchorX = settings.anchorX * displayWidth;
    config.aim.anchorY = settings.anchorY * displayHeight;
    config.aim.anchorRadiusPx = settings.anchorRadius;

    config.cameraGuess.gainX = settings.sceneGain;
    config.cameraGuess.gainY = settings.sceneGain;
    config.cameraGuess.delayS = settings.actuationDelayMs * 0.001f;
    config.cameraGuess.invertY = settings.invertY;
    config.learnCamera = settings.autoCalibrate;
    config.hasLearned = settings.learnedConfidence > 0.0f && settings.learnedGainX > 0.0f && settings.learnedGainY > 0.0f;
    config.learned.gainX = settings.learnedGainX;
    config.learned.gainY = settings.learnedGainY;
    config.learned.delayS = settings.learnedDelayMs * 0.001f;
    config.learnedConfidence = settings.learnedConfidence;
    return config;
}

/// Keeps what the aim loop measured about the game's camera, so the next run starts close to right.
void keepLearnedCamera(const aimbuddy::CameraResponse& response, float confidence) {
    Settings settings = g_settingsStore.get();
    const bool unchanged = std::fabs(settings.learnedGainX - response.gainX) < 0.05f * response.gainX &&
                           std::fabs(settings.learnedGainY - response.gainY) < 0.05f * response.gainY &&
                           std::fabs(settings.learnedDelayMs - response.delayS * 1000.0f) < 4.0f;
    if (unchanged) {
        return;
    }
    settings.learnedGainX = response.gainX;
    settings.learnedGainY = response.gainY;
    settings.learnedDelayMs = response.delayS * 1000.0f;
    settings.learnedConfidence = confidence;
    g_settingsStore.set(settings);
}

}  // namespace

aimbuddy::DetectionPipeline& aimbuddy::pipeline() { return g_pipeline; }
aimbuddy::SettingsStore& aimbuddy::settingsStore() { return g_settingsStore; }
aimbuddy::touch::TouchLink& aimbuddy::touchLink() { return g_touchLink; }
aimbuddy::AimLoop& aimbuddy::aimLoop() { return g_aimLoop; }
const std::string& aimbuddy::settingsPath() { return g_settingsPath; }

extern "C" {

/// The part of the screen the detector is looking at, as a rectangle in display pixels.
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

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    LOGI("Native library loaded");
    g_jvm = vm;
    // What the camera did on screen is known to the tracker and written by the pipeline.
    g_pipeline.tracker().setEgoMotion(&g_cameraMotion);
    g_pipeline.setCameraMotion(&g_cameraMotion);
    ncnn::create_gpu_instance();
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* /* vm */, void* /* reserved */) {
    LOGI("Native library unloading");
    ncnn::destroy_gpu_instance();
}

/**
 * @brief Loads the player's settings, once per process; later calls do nothing.
 *
 * The app's screen and the assist service both call it. Reading the file again while the assist
 * runs would undo changes the player just made in the menu, so the first read wins.
 *
 * @param settingsPath File the settings are read from and written to (in the app's private storage)
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_NativeBridge_loadSettings(JNIEnv* env, jclass /* clazz */, jstring settingsPath) {
    if (g_settingsLoaded) {
        return;
    }
    g_settingsLoaded = true;
    g_settingsPath = toStdString(env, settingsPath);
    if (g_settingsStore.load(g_settingsPath)) {
        LOGI("Loaded settings from %s", g_settingsPath.c_str());
    } else {
        LOGI("Using default settings");
    }
}

/// All settings as `name=value` lines, for the app's screens.
JNIEXPORT jstring JNICALL Java_com_aimbuddy_NativeBridge_settingsSnapshot(JNIEnv* env, jclass /* clazz */) {
    const std::string text = aimbuddy::SettingsStore::serialize(g_settingsStore.get());
    return env->NewStringUTF(text.c_str());
}

/// Changes one setting by name. Unknown names and unreadable numbers change nothing; values are kept in range.
JNIEXPORT void JNICALL Java_com_aimbuddy_NativeBridge_setSetting(JNIEnv* env, jclass /* clazz */, jstring name, jstring value) {
    g_settingsStore.set(
        aimbuddy::SettingsStore::withValue(g_settingsStore.get(), toStdString(env, name), toStdString(env, value)));
}

/// Puts every setting back to how a fresh install starts, keeping the language.
JNIEXPORT void JNICALL Java_com_aimbuddy_NativeBridge_resetSettings(JNIEnv* /* env */, jclass /* clazz */) {
    g_settingsStore.set(g_settingsStore.get().resetKeepingLanguage());
}

/**
 * @brief Loads the detection model and tells the detector what its classes mean
 * @param paramPath Path of the .param file, or null to use the model bundled in the app
 * @param binPath Path of the .bin file, or null
 * @param roles Comma separated roles in class order, such as "body,head"; null or empty when unknown
 * @param endToEnd True if the model already removes duplicate detections
 * @param inputSize Width and height of the network input, or 0 to work it out
 * @return false if the model could not be used
 */
JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_NativeBridge_loadModel(JNIEnv* env, jclass /* clazz */, jobject assetManager, jstring paramPath,
                                         jstring binPath, jstring roles, jboolean endToEnd, jint inputSize) {
    if (g_pipeline.isRunning()) {
        LOGE("Cannot load a model while running");
        return JNI_FALSE;
    }
    AAssetManager* assets = AAssetManager_fromJava(env, assetManager);
    if (assets == nullptr) {
        LOGE("Failed to get the asset manager");
        return JNI_FALSE;
    }

    const std::string roleList = toStdString(env, roles);
    aimbuddy::ModelProfile profile =
        roleList.empty() ? aimbuddy::ModelProfile() : aimbuddy::ModelProfile::parseRoles(roleList, 0);
    profile.endToEndOutput = endToEnd == JNI_TRUE;
    profile.inputSize = inputSize > 0 ? inputSize : 0;
    LOGI("Model profile: roles='%s' endToEnd=%d inputSize=%d", roleList.c_str(), endToEnd == JNI_TRUE, inputSize);

    g_modelLoaded = g_pipeline.loadModel(assets, toStdString(env, paramPath), toStdString(env, binPath), profile);
    if (!g_modelLoaded) {
        LOGE("Failed to load the detection model");
    }
    return g_modelLoaded ? JNI_TRUE : JNI_FALSE;
}

/**
 * @brief Tells the native side how large the screen and the capture image are
 * @param displayWidth Display width in pixels, in the orientation the player sees
 * @param displayHeight Display height in pixels
 * @param captureWidth ImageReader width in pixels
 * @param captureHeight ImageReader height in pixels
 * @param rotation Display rotation, 0 to 3, as Android numbers it
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_NativeBridge_configureDisplay(JNIEnv* /* env */, jclass /* clazz */, jint displayWidth,
                                                jint displayHeight, jint captureWidth, jint captureHeight,
                                                jint rotation) {
    aimbuddy::CaptureGeometry geometry;
    geometry.screenWidth = displayWidth;
    geometry.screenHeight = displayHeight;
    geometry.captureWidth = captureWidth;
    geometry.captureHeight = captureHeight;
    g_pipeline.capture().configure(geometry);
    g_aimLoop.setDisplay(displayWidth, displayHeight, rotation);
    LOGI("Display %dx%d rotation %d, capture %dx%d", displayWidth, displayHeight, rotation, captureWidth,
         captureHeight);
}

/** @brief Starts detection and, if the assist is switched on, the aim loop. */
JNIEXPORT jboolean JNICALL Java_com_aimbuddy_NativeBridge_start(JNIEnv* /* env */, jclass /* clazz */) {
    if (!g_pipeline.isModelLoaded()) {
        LOGE("Cannot start: no model loaded");
        return JNI_FALSE;
    }
    // The assist starts off on every run. It moves the aim by itself, so the player turns it on in the game.
    Settings settings = g_settingsStore.get();
    settings.assistEnabled = false;
    g_settingsStore.set(settings);
    g_cameraMotion.clear();
    g_pipeline.start();
    g_aimLoop.setConfigSource(makeAimLoopConfig);
    g_aimLoop.setLearnedSink(keepLearnedCamera);
    g_aimLoop.start();
    return JNI_TRUE;
}

/** @brief Stops the aim loop (the finger comes off and the touchscreen goes back), then detection. */
JNIEXPORT void JNICALL Java_com_aimbuddy_NativeBridge_stop(JNIEnv* /* env */, jclass /* clazz */) {
    g_aimLoop.stop();
    g_pipeline.stop();
}

JNIEXPORT jboolean JNICALL Java_com_aimbuddy_NativeBridge_isRunning(JNIEnv* /* env */, jclass /* clazz */) {
    return g_pipeline.isRunning() ? JNI_TRUE : JNI_FALSE;
}

/** @brief Stops everything, closes the touch connection and saves the settings. */
JNIEXPORT void JNICALL Java_com_aimbuddy_NativeBridge_shutdown(JNIEnv* /* env */, jclass /* clazz */) {
    g_aimLoop.stop();
    g_pipeline.stop();
    g_touchLink.disconnect();
    if (!g_settingsPath.empty() && !g_settingsStore.saveIfChanged(g_settingsPath)) {
        LOGE("Failed to save the settings");
    }
    LOGI("Native shutdown complete");
}

/**
 * @brief Handles one frame from screen capture
 * @param hardwareBuffer AHardwareBuffer from ImageReader; only valid during this call
 */
JNIEXPORT void JNICALL
Java_com_aimbuddy_NativeBridge_onFrame(JNIEnv* env, jclass /* clazz */, jobject hardwareBuffer, jlong timestamp) {
    if (!g_pipeline.isRunning() || !hardwareBuffer) {
        return;
    }
    AHardwareBuffer* buffer = AHardwareBuffer_fromHardwareBuffer(env, hardwareBuffer);
    if (!buffer) {
        LOGW("Failed to get AHardwareBuffer");
        return;
    }
    const Settings settings = g_settingsStore.get();
    g_pipeline.setConfidenceThreshold(settings.confidence);
    // The region is copied before this call returns, so the buffer can be recycled right away.
    g_pipeline.capture().setZoneRadius(settings.detectionZoneRadius);
    g_pipeline.capture().ingest(buffer, static_cast<int64_t>(timestamp));
}

/**
 * @brief Connects to the touch helper the app started
 * @param port Loopback port the helper listens on
 * @param tokenHex The helper's secret, 16 hexadecimal digits
 * @return 0 when connected, otherwise a touch::protocol::Reason saying why not
 */
JNIEXPORT jint JNICALL
Java_com_aimbuddy_NativeBridge_touchConnect(JNIEnv* env, jclass /* clazz */, jint port, jstring tokenHex) {
    const std::string text = toStdString(env, tokenHex);
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 16);
    if (text.size() != 16 || end == nullptr || *end != '\0') {
        return static_cast<jint>(aimbuddy::touch::protocol::Reason::kBadClient);
    }
    aimbuddy::touch::protocol::Token token;
    token.first = static_cast<int32_t>(static_cast<uint32_t>(value >> 32));
    token.second = static_cast<int32_t>(static_cast<uint32_t>(value & 0xFFFFFFFFu));
    constexpr int kConnectTimeoutMs = 6000;
    return static_cast<jint>(g_touchLink.connect(port, token, kConnectTimeoutMs));
}

/** @brief The player's choice of route to the touchscreen: 0 automatic, 1 Shizuku, 2 root. */
JNIEXPORT jint JNICALL Java_com_aimbuddy_NativeBridge_touchBackend(JNIEnv* /* env */, jclass /* clazz */) {
    return static_cast<jint>(g_settingsStore.get().touchBackend);
}

/** @brief Lets the touch helper go; it gives the touchscreen back and stops. */
JNIEXPORT void JNICALL Java_com_aimbuddy_NativeBridge_touchDisconnect(JNIEnv* /* env */, jclass /* clazz */) {
    g_touchLink.disconnect();
}

/**
 * @brief What the touch helper last reported
 * @return state in the low byte (touch::protocol::State), reason in the next 16 bits; 1 << 24 is set while connected
 */
JNIEXPORT jint JNICALL Java_com_aimbuddy_NativeBridge_touchStatus(JNIEnv* /* env */, jclass /* clazz */) {
    const int connected = g_touchLink.isConnected() ? 1 : 0;
    return (connected << 24) | (static_cast<int>(g_touchLink.reason()) << 8) | static_cast<int>(g_touchLink.state());
}

/**
 * Calls a static method of NativeBridge from native code, attaching the calling thread to the
 * JVM if it is not already. `argument` is passed when the method takes one boolean.
 */
static void callBridge(const char* method, const char* signature, bool hasArgument, bool argument) {
    if (!g_jvm) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return;
        }
        attached = true;
    }
    jclass bridge = env->FindClass("com/aimbuddy/NativeBridge");
    if (bridge) {
        jmethodID id = env->GetStaticMethodID(bridge, method, signature);
        if (id) {
            if (hasArgument) {
                env->CallStaticVoidMethod(bridge, id, static_cast<jboolean>(argument));
            } else {
                env->CallStaticVoidMethod(bridge, id);
            }
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(bridge);
    }
    if (attached) g_jvm->DetachCurrentThread();
}

/**
 * Tells Kotlin the player changed the streamer mode setting, so the overlay windows can be marked
 * secure or not. Called from the overlay's render thread.
 */
void NotifyStreamerModeChanged(bool enabled) { callBridge("onStreamerModeChanged", "(Z)V", true, enabled); }

/** Tells Kotlin the player pressed "Stop" in the overlay menu. Called from the overlay's render thread. */
void NotifyStopRequested() { callBridge("onStopRequested", "()V", false, false); }

}  // extern "C"
