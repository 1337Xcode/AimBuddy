/**
 * imgui_menu.cpp - Native ImGui menu implementation for GLSurfaceView
 * 
 * This provides the JNI bridge for ImGuiGLSurface.kt, handling:
 * - ImGui initialization with Android backend
 * - Menu rendering with settings controls
 * - Touch event processing
 */

#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_android.h"
#include "imgui/imgui_impl_opengl3.h"
#include "settings.h"
#include "utils/logger.h"
#include "utils/vector2.h"
#include "utils/imgui_helper.h"
#include "renderer/esp_renderer.h"
#include "utils/aimbot_types.h"
#include "utils/detection_zone.h"
#include "renderer/box_smoothing.h"
#include "detector/yolo_detector.h"

// Forward declaration for shared config access (defined in esp_jni.cpp)
extern "C" ESP::RenderConfig* GetRenderConfig();
extern "C" bool GetLatestResultSnapshot(ESP::DetectionResult* out);
extern "C" void UpdateScreenSize(int width, int height);
extern "C" void GetCaptureSize(int* outWidth, int* outHeight);
extern UnifiedSettings g_settings;

// Global state for ImGui menu
static ANativeWindow* g_menuWindow = nullptr;
static bool g_imguiInitialized = false;
static int g_screenWidth = 0;
static int g_screenHeight = 0;
static bool g_menuVisible = false;
static std::atomic<bool> g_rootAvailable{false};  // Track root status

// Icon position synced from Kotlin layer (existing SVG icon)
static ImVec2 g_iconPos = ImVec2(60.0f, 200.0f);  // Initial default
static constexpr float ICON_RADIUS = 44.0f;  // Match Kotlin icon size (44dp)

// Box smoothing for stable, jitter-free rendering
static ESP::BoxSmoother g_boxSmoother;
static std::array<ESP::BoundingBox, Config::MAX_DETECTIONS> g_smoothedBoxes;
static int g_smoothedCount = 0;
static bool g_settingsPendingSave = false;
static double g_settingsDirtyAt = 0.0;
static constexpr double SETTINGS_SAVE_DELAY_SEC = 0.35;
static std::chrono::steady_clock::time_point g_lastOverlayTickTime{};
static float g_measuredOverlayFps = 0.0f;
static float g_measuredInferenceMs = 0.0f;
static ImVec2 g_menuSize = ImVec2(0.0f, 0.0f);
static bool g_menuWasVisible = false;

static float QuantizeStep(float value, float step) {
    if (step <= 0.0f) return value;
    return std::round(value / step) * step;
}

static void ShowSettingHelp(const char* description) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void ApplyRenderConfigToUnifiedSettings(const ESP::RenderConfig& settings) {
    g_settings.boxColorR = settings.boxColorR.load(std::memory_order_relaxed);
    g_settings.boxColorG = settings.boxColorG.load(std::memory_order_relaxed);
    g_settings.boxColorB = settings.boxColorB.load(std::memory_order_relaxed);
    g_settings.boxThickness = settings.boxThickness.load(std::memory_order_relaxed);
    g_settings.confidenceThreshold = settings.confidenceThreshold.load(std::memory_order_relaxed);
    g_settings.fovRadius = settings.fovRadius.load(std::memory_order_relaxed);
    g_settings.showFPS = settings.showFPS.load(std::memory_order_relaxed);
    g_settings.showDetectionCount = settings.showDetectionCount.load(std::memory_order_relaxed);
    g_settings.showLabels = settings.showLabels.load(std::memory_order_relaxed);
    g_settings.drawLine = settings.drawLine.load(std::memory_order_relaxed);
    g_settings.drawDot = settings.drawDot.load(std::memory_order_relaxed);
    g_settings.enableSmoothing = settings.enableSmoothing.load(std::memory_order_relaxed);
    g_settings.smoothingFactor = settings.smoothingFactor.load(std::memory_order_relaxed);
    g_settings.aimbotEnabled = settings.aimbotEnabled.load(std::memory_order_relaxed);
    g_settings.headOffset = settings.headOffset.load(std::memory_order_relaxed);

    g_settings.screenWidth = g_screenWidth;
    g_settings.screenHeight = g_screenHeight;
    if (g_screenWidth > 0 && g_screenHeight > 0) {
        float ratioX = settings.touchCenterX.load(std::memory_order_relaxed);
        float ratioY = settings.touchCenterY.load(std::memory_order_relaxed);
        g_settings.touchX = ratioX * static_cast<float>(g_screenWidth);
        g_settings.touchY = ratioY * static_cast<float>(g_screenHeight);
    }
    g_settings.touchRadius = settings.touchRadius.load(std::memory_order_relaxed);
    g_settings.aimDelay = settings.aimDelay.load(std::memory_order_relaxed);

    g_settings.validate();
}

// Initialize ImGui for GLSurfaceView rendering
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeInit(JNIEnv* env, jclass /* this */, jobject assetManager, jobject surface) {
    LOGI("nativeImGuiInit called");
    (void)assetManager;
    
    if (g_imguiInitialized) {
        LOGI("ImGui already initialized, skipping");
        return;
    }

    if (!surface) {
        LOGE("Surface is null");
        return;
    }

    // Get native window from Surface
    g_menuWindow = ANativeWindow_fromSurface(env, surface);
    if (!g_menuWindow) {
        LOGE("Failed to get ANativeWindow from Surface");
        return;
    }

    LOGI("Got ANativeWindow: %p", g_menuWindow);

    // Query dimensions
    g_screenWidth = ANativeWindow_getWidth(g_menuWindow);
    g_screenHeight = ANativeWindow_getHeight(g_menuWindow);
    LOGI("Menu window: %dx%d", g_screenWidth, g_screenHeight);


    // Create ImGui context if not already created
    ImGuiContext* existingContext = ImGui::GetCurrentContext();
    if (!existingContext) {
        LOGI("Creating new ImGui context");
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
    } else {
        LOGI("Using existing ImGui context");
    }
    
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // No ini file
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.DisplaySize = ImVec2(static_cast<float>(g_screenWidth), static_cast<float>(g_screenHeight));
    
    // Dark theme with accent colors
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.95f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.9f, 0.2f, 0.3f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.95f, 0.3f, 0.4f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.6f, 0.45f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.2f, 0.7f, 0.55f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.9f, 0.3f, 0.4f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.2f, 0.9f, 0.5f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.3f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.4f, 1.0f);
    
    // Scale for mobile without making controls overly cramped
    style.ScaleAllSizes(1.15f);
    io.FontGlobalScale = 1.08f;
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.WindowBorderSize = 1.0f;
    style.ScrollbarSize = 18.0f;
    
    // Initialize backends
    LOGI("Initializing ImGui Android backend");
    ImGui_ImplAndroid_Init(g_menuWindow);
    
    LOGI("Initializing ImGui OpenGL3 backend");
    ImGui_ImplOpenGL3_Init("#version 300 es");
    
    g_imguiInitialized = true;
    LOGI("ImGui menu initialized successfully");
}

    // Handle surface size changes
    extern "C" JNIEXPORT void JNICALL
    Java_com_aimbuddy_ImGuiGLSurface_nativeSurfaceChanged(JNIEnv* /* env */, jclass /* this */, jint width, jint height) {
        if (!g_imguiInitialized) {
            return;
        }

        g_screenWidth = width;
        g_screenHeight = height;
        glViewport(0, 0, width, height);

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));

        UpdateScreenSize(width, height);
    }

// Render ImGui (menu + ESP)
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeTick(JNIEnv* /* env */, jclass /* this */) {
    if (!g_imguiInitialized || !g_menuWindow) {
        return;
    }

    try {
        const auto nowTick = std::chrono::steady_clock::now();
        if (g_lastOverlayTickTime.time_since_epoch().count() != 0) {
            const float dtSeconds = std::chrono::duration<float>(nowTick - g_lastOverlayTickTime).count();
            if (dtSeconds > 0.0f && dtSeconds <= 0.25f) {
                const float instantFps = 1.0f / dtSeconds;
                g_measuredOverlayFps = (g_measuredOverlayFps > 0.0f)
                    ? (g_measuredOverlayFps * 0.90f + instantFps * 0.10f)
                    : instantFps;
            }
        }
        g_lastOverlayTickTime = nowTick;

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplAndroid_NewFrame();
        ImGui::NewFrame();

        // Access global settings
        ESP::RenderConfig* settings = GetRenderConfig();
        if (!settings) {
            ImGui::EndFrame();
            ImGui::Render();
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            return;
        }


        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        float displayW = displaySize.x;
        float displayH = displaySize.y;

        // Always-visible center crosshair
        {
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            const float centerX = displayW * 0.5f;
            const float centerY = displayH * 0.5f;
            const float crossArm = 11.5f;
            const ImU32 crossColor = IM_COL32(58, 156, 255, 240);
            drawList->AddLine(ImVec2(centerX - crossArm, centerY), ImVec2(centerX + crossArm, centerY), crossColor, 3.0f);
            drawList->AddLine(ImVec2(centerX, centerY - crossArm), ImVec2(centerX, centerY + crossArm), crossColor, 3.0f);
        }

        // Get detection results and apply smoothing if enabled
        ESP::DetectionResult latest;
        bool hasDetections = GetLatestResultSnapshot(&latest);
        if (latest.inferenceTimeMs > 0.01f && std::isfinite(latest.inferenceTimeMs)) {
            g_measuredInferenceMs = (g_measuredInferenceMs > 0.0f)
                ? (g_measuredInferenceMs * 0.88f + latest.inferenceTimeMs * 0.12f)
                : latest.inferenceTimeMs;
        }
        
        // Apply smoothing if enabled
        bool useSmoothing = settings->enableSmoothing.load(std::memory_order_relaxed);
        const ESP::BoundingBox* boxesToRender = nullptr;
        int boxCount = 0;
        
        // Temp buffer for BoxSmoother adaptation (FixedArray -> std::array)
        std::array<ESP::BoundingBox, Config::MAX_DETECTIONS> tempInputs;
        
        if (hasDetections) {
            if (useSmoothing) {
                float alpha = settings->smoothingFactor.load(std::memory_order_relaxed);
                // Copy to std::array for BoxSmoother signature compatibility
                std::copy(latest.boxes.begin(), latest.boxes.end(), tempInputs.begin());
                g_boxSmoother.update(tempInputs, latest.boxes.size(), g_smoothedBoxes, g_smoothedCount, alpha);
                boxesToRender = g_smoothedBoxes.data();
                boxCount = g_smoothedCount;
            } else {
                boxesToRender = latest.boxes.data();
                boxCount = latest.boxes.size();
            }
        } else if (useSmoothing) {
            // Update smoother with zero detections to age out stale tracks
            std::array<ESP::BoundingBox, Config::MAX_DETECTIONS> emptyArr{};
            g_boxSmoother.update(emptyArr, 0, g_smoothedBoxes, g_smoothedCount, 0.5f);
            boxesToRender = g_smoothedBoxes.data();
            boxCount = g_smoothedCount;
        }
        
        if (boxCount > 0 && boxesToRender) {
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            float r = settings->boxColorR.load(std::memory_order_relaxed);
            float g = settings->boxColorG.load(std::memory_order_relaxed);
            float b = settings->boxColorB.load(std::memory_order_relaxed);
            int thickness = settings->boxThickness.load(std::memory_order_relaxed);
            float threshold = settings->confidenceThreshold.load(std::memory_order_relaxed);
            bool showLabels = settings->showLabels.load(std::memory_order_relaxed);
            bool drawLine = settings->drawLine.load(std::memory_order_relaxed);
            bool drawDot = settings->drawDot.load(std::memory_order_relaxed);
            float headOffset = settings->headOffset.load(std::memory_order_relaxed);
            const float espFovRadius = settings->fovRadius.load(std::memory_order_relaxed);

            thickness = std::max(1, std::min(thickness, 5));
            ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
            ImU32 shadowColor = IM_COL32(0, 0, 0, 150);
            
            // Track closest enemy for snap line
            int closestEnemyIdx = -1;
            float closestDistSq = espFovRadius * espFovRadius;
            float centerX = displayW * 0.5f;
            float centerY = displayH * 0.5f;

            for (int i = 0; i < boxCount; ++i) {
                const ESP::BoundingBox& box = boxesToRender[i];
                if (box.confidence < threshold || box.width <= 0.0f || box.height <= 0.0f) {
                    continue;
                }

                float left = box.x;
                float top = box.y;
                float right = box.x + box.width;
                float bottom = box.y + box.height;

                // Clamp to screen bounds
                if (left < 0.0f) left = 0.0f;
                if (top < 0.0f) top = 0.0f;
                if (right > displayW) right = displayW;
                if (bottom > displayH) bottom = displayH;

                // Draw box with shadow for depth
                ESP::ImGuiHelper::DrawBox3D(
                    drawList,
                    ImVec2(left, top),
                    ImVec2(right, bottom),
                    boxColor,
                    static_cast<float>(thickness),
                    shadowColor
                );
                
                // Calculate head position and box center
                float boxCenterX = left + (right - left) * 0.5f;
                float headY = top + (bottom - top) * headOffset;
                
                // Draw head dot if enabled
                if (drawDot) {
                    drawList->AddCircleFilled(
                        ImVec2(boxCenterX, headY),
                        5.0f + thickness * 0.5f,
                        boxColor,
                        12
                    );
                }
                
                // Track closest enemy for snap line (within FOV)
                float dx = boxCenterX - centerX;
                float dy = headY - centerY;
                float distSq = dx * dx + dy * dy;
                if (distSq < closestDistSq) {
                    closestDistSq = distSq;
                    closestEnemyIdx = i;
                }

                if (showLabels) {
                    // Top-center label: "Enemy" with shadow
                    const char* enemyLabel = "Enemy";
                    ImVec2 enemySize = ImGui::CalcTextSize(enemyLabel);
                    ImVec2 enemyPos(
                        left + (right - left - enemySize.x) * 0.5f,
                        top - enemySize.y - 4.0f
                    );
                    if (enemyPos.y < 0.0f) enemyPos.y = top + 2.0f;
                    ESP::ImGuiHelper::DrawTextWithShadow(drawList, enemyPos, boxColor, enemyLabel);

                    // Bottom-center accuracy with shadow
                    char accLabel[32];
                    snprintf(accLabel, sizeof(accLabel), "%.0f%%", box.confidence * 100.0f);
                    ImVec2 accSize = ImGui::CalcTextSize(accLabel);
                    ImVec2 accPos(
                        left + (right - left - accSize.x) * 0.5f,
                        bottom + 2.0f
                    );
                    if (accPos.y + accSize.y > displayH) accPos.y = bottom - accSize.y - 2.0f;
                    ESP::ImGuiHelper::DrawTextWithShadow(drawList, accPos, boxColor, accLabel);
                }
            }
            
            // Draw snap line to closest enemy
            if (drawLine && closestEnemyIdx >= 0) {
                const ESP::BoundingBox& enemyBox = boxesToRender[closestEnemyIdx];
                float boxCenterX = enemyBox.x + enemyBox.width * 0.5f;
                float headY = enemyBox.y + enemyBox.height * headOffset;
                
                ImU32 snapLineColor = IM_COL32(255, 100, 50, 255);
                drawList->AddLine(
                    ImVec2(centerX, centerY),
                    ImVec2(boxCenterX, headY),
                    snapLineColor,
                    2.5f
                );
            }
        }

        // Draw FOV overlays (ESP=blue box, Aimbot=red circle)
        {
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            const bool aimbotEnabled = settings->aimbotEnabled.load(std::memory_order_relaxed);
            const float espFovRadius = settings->fovRadius.load(std::memory_order_relaxed);
            const float aimFovRadius = std::min(
                (g_settings.aimFovRadius > 0.0f) ? g_settings.aimFovRadius : espFovRadius,
                espFovRadius
            );

            if (espFovRadius > 0.0f) {
                const float centerX = displayW * 0.5f;
                const float centerY = displayH * 0.5f;

                int captureWidth = Config::CAPTURE_WIDTH;
                int captureHeight = Config::CAPTURE_HEIGHT;
                GetCaptureSize(&captureWidth, &captureHeight);
                const ESP::DetectionZoneMetrics zone = ESP::ComputeDetectionZoneMetrics(
                    espFovRadius,
                    g_screenWidth,
                    displayW,
                    displayH,
                    captureWidth,
                    captureHeight
                );

                const ImU32 espFovColor = IM_COL32(40, 140, 255, 220);
                const ImVec2 tl(centerX - zone.halfWidthPx, centerY - zone.halfHeightPx);
                const ImVec2 br(centerX + zone.halfWidthPx, centerY + zone.halfHeightPx);
                drawList->AddRect(tl, br, espFovColor, 0.0f, 0, 2.2f);

                if (aimbotEnabled && aimFovRadius > 0.0f) {
                    drawList->AddCircle(ImVec2(centerX, centerY), aimFovRadius, IM_COL32(255, 60, 60, 230), 64, 2.3f);
                }
            }
        }

        // Detection count overlay (red, centered at top with margin)
        int detCount = latest.boxes.size();
        if (settings->showDetectionCount.load(std::memory_order_relaxed) && detCount > 0) {
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            ImU32 redColor = IM_COL32(255, 50, 50, 255);
            ImFont* font = ImGui::GetFont();
            if (drawList != nullptr && font != nullptr) {
                float largeSize = ImGui::GetFontSize() * 2.0f;
                float topMargin = 40.0f;  // Proper margin to avoid clipping

                // Format: "X enemy" or "X enemies"
                char countText[64];
                const char* label = (detCount == 1) ? "enemy" : "enemies";
                snprintf(countText, sizeof(countText), "%d %s", detCount, label);

                ImVec2 textSize = font->CalcTextSizeA(largeSize, FLT_MAX, 0.0f, countText);
                ImVec2 textPos((displayW - textSize.x) * 0.5f, topMargin);
                drawList->AddText(font, largeSize, textPos, redColor, countText);
            }
        }

        // Menu window — display-proportional size with scrollable tab layout
        g_menuVisible = settings->menuVisible.load(std::memory_order_relaxed);
        if (g_menuVisible) {
            bool settingsDirty = false;
            const float defaultMenuWidth  = std::max(460.0f, std::min(displayW * 0.48f, 860.0f));
            const float defaultMenuHeight = std::max(560.0f, std::min(displayH * 0.92f, 1060.0f));
            if (!g_menuWasVisible || g_menuSize.x <= 0.0f || g_menuSize.y <= 0.0f) {
                g_menuSize = ImVec2(defaultMenuWidth, defaultMenuHeight);
            }

            const float menuWidth = g_menuSize.x;
            const float menuHeight = g_menuSize.y;
            const float iconPad = 52.0f;
            float menuX = g_iconPos.x + ICON_RADIUS + iconPad;
            float menuY = g_iconPos.y - menuHeight * 0.5f;
            if (menuX + menuWidth > displayW)
                menuX = g_iconPos.x - ICON_RADIUS - iconPad - menuWidth;
            menuX = std::max(4.0f, std::min(menuX, displayW - menuWidth - 4.0f));
            menuY = std::max(4.0f, std::min(menuY, displayH - menuHeight - 4.0f));

            ImGui::SetNextWindowPos(ImVec2(menuX, menuY), ImGuiCond_Always);
            ImGui::SetNextWindowSize(g_menuSize, ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 460.0f), ImVec2(displayW - 8.0f, displayH - 8.0f));

            ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;
            if (ImGui::Begin("AimBuddy v0.1.0-beta.1", nullptr, windowFlags)) {
                g_menuSize = ImGui::GetWindowSize();
                const bool rootAvailable = g_rootAvailable.load(std::memory_order_relaxed);
                if (!rootAvailable) {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Root not granted (ESP mode)");
                }

                ImGui::Separator();
                ImGui::BeginChild("##MenuScroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 6.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

                if (ImGui::BeginTabBar("##MenuTabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
                    if (ImGui::BeginTabItem("ESP")) {
                        bool showLabels = settings->showLabels.load(std::memory_order_relaxed);
                        bool drawLine = settings->drawLine.load(std::memory_order_relaxed);
                        bool drawDot = settings->drawDot.load(std::memory_order_relaxed);
                        bool countOn = settings->showDetectionCount.load(std::memory_order_relaxed);
                        bool smoothOn = settings->enableSmoothing.load(std::memory_order_relaxed);

                        if (ImGui::Checkbox("Labels", &showLabels)) { settings->showLabels.store(showLabels, std::memory_order_relaxed); settingsDirty = true; }
                        ShowSettingHelp("Shows label text over each detected target.");
                        if (ImGui::Checkbox("Snap line", &drawLine)) { settings->drawLine.store(drawLine, std::memory_order_relaxed); settingsDirty = true; }
                        ShowSettingHelp("Draws a line from your screen center to the target box.");
                        if (ImGui::Checkbox("Head dot", &drawDot)) { settings->drawDot.store(drawDot, std::memory_order_relaxed); settingsDirty = true; }
                        ShowSettingHelp("Marks the estimated head point for each detection.");
                        if (ImGui::Checkbox("Detection count", &countOn)) { settings->showDetectionCount.store(countOn, std::memory_order_relaxed); settingsDirty = true; }
                        ShowSettingHelp("Shows how many targets are currently detected.");
                        ImGui::Separator();

                        if (ImGui::Checkbox("Enable ESP box smoothing", &smoothOn)) { settings->enableSmoothing.store(smoothOn, std::memory_order_relaxed); settingsDirty = true; }
                        ShowSettingHelp("Stabilizes box movement to reduce jitter between frames.");
                        if (smoothOn) {
                            float smooth = settings->smoothingFactor.load(std::memory_order_relaxed);
                            if (ImGui::SliderFloat("Smoothing amount", &smooth, 0.10f, 1.0f, "%.2f")) {
                                settings->smoothingFactor.store(smooth, std::memory_order_relaxed);
                                settingsDirty = true;
                            }
                            ShowSettingHelp("Lower values react faster; higher values are smoother but lag more.");
                        }

                        ImGui::Separator();

                        float boxColor[4] = {
                            settings->boxColorR.load(std::memory_order_relaxed),
                            settings->boxColorG.load(std::memory_order_relaxed),
                            settings->boxColorB.load(std::memory_order_relaxed),
                            1.0f
                        };
                        if (ImGui::ColorEdit4("Box color", boxColor, ImGuiColorEditFlags_NoInputs)) {
                            settings->boxColorR.store(boxColor[0], std::memory_order_relaxed);
                            settings->boxColorG.store(boxColor[1], std::memory_order_relaxed);
                            settings->boxColorB.store(boxColor[2], std::memory_order_relaxed);
                            settingsDirty = true;
                        }
                        ShowSettingHelp("Changes ESP box color for better visibility.");

                        float thickness = static_cast<float>(settings->boxThickness.load(std::memory_order_relaxed));
                        if (ImGui::SliderFloat("Box thickness", &thickness, 1.0f, 5.0f, "%.0f")) {
                            settings->boxThickness.store(static_cast<int>(thickness), std::memory_order_relaxed);
                            settingsDirty = true;
                        }
                        ShowSettingHelp("Higher values make boxes easier to see; lower values look cleaner.");

                        ImGui::Separator();

                        float conf = settings->confidenceThreshold.load(std::memory_order_relaxed);
                        if (ImGui::SliderFloat("Confidence", &conf, 0.1f, 0.95f, "%.2f")) {
                            settings->confidenceThreshold.store(conf, std::memory_order_relaxed);
                            settingsDirty = true;
                        }
                        ShowSettingHelp("Minimum detection confidence. Higher reduces false positives.");

                        float detFov = settings->fovRadius.load(std::memory_order_relaxed);
                        if (ImGui::SliderFloat("Detection zone", &detFov, 100.0f, 650.0f, "%.0f px")) {
                            settings->fovRadius.store(detFov, std::memory_order_relaxed);
                            if (g_settings.aimFovRadius > detFov) {
                                g_settings.aimFovRadius = detFov;
                            }
                            settingsDirty = true;
                        }
                        ShowSettingHelp("Limits detection processing to a center region for more stable targeting.");

                        bool showTouchZone = g_settings.showTouchZone;
                        if (ImGui::Checkbox("Touch zone overlay", &showTouchZone)) {
                            g_settings.showTouchZone = showTouchZone;
                            settingsDirty = true;
                        }
                        ShowSettingHelp("Shows the touch input area used for assist movement.");
                        if (g_settings.showTouchZone) {
                            float alpha = g_settings.touchZoneAlpha;
                            if (ImGui::SliderFloat("Touch zone opacity", &alpha, 0.10f, 1.0f, "%.2f")) {
                                g_settings.touchZoneAlpha = QuantizeStep(alpha, 0.01f);
                                settingsDirty = true;
                            }
                            ShowSettingHelp("Lower opacity is less distracting; higher opacity is easier to locate.");
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Aim")) {
                        if (!rootAvailable) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
                            ImGui::TextUnformatted("Root access required for assisted input controls.");
                            ImGui::PopStyleColor();
                            ImGui::TextDisabled("Grant root in Magisk/KernelSU and reopen Start flow.");
                        } else {
                            bool enabled = settings->aimbotEnabled.load(std::memory_order_relaxed);
                            if (ImGui::Checkbox("Enable Aim Assist", &enabled)) {
                                settings->aimbotEnabled.store(enabled, std::memory_order_relaxed);
                                settingsDirty = true;
                            }

                            if (enabled) {
                                ImGui::Spacing();
                                if (ImGui::Button("Default", ImVec2(150, 0))) {
                                    g_settings.aimMode = 0; g_settings.aimSpeed = 0.48f;
                                    g_settings.smoothness = 0.78f; g_settings.filterType = 1;
                                    g_settings.emaAlpha = 0.25f; g_settings.pdDerivativeGain = 0.040f;
                                    g_settings.velocityLeadFactor = 0.22f; g_settings.velocityLeadClamp = 18.0f;
                                    g_settings.enableConvergenceDamping = true; g_settings.convergenceRadius = 30.0f;
                                    g_settings.maxLockMissFrames = 2; g_settings.targetSwitchDelayFrames = 8;
                                    g_settings.recoilCompensationEnabled = false;
                                    g_settings.aimFovRadius = 240.0f;
                                    settings->headOffset.store(0.18f, std::memory_order_relaxed);
                                    settingsDirty = true;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Competitive", ImVec2(150, 0))) {
                                    g_settings.aimMode = 1; g_settings.aimSpeed = 0.72f;
                                    g_settings.smoothness = 0.45f; g_settings.filterType = 0;
                                    g_settings.emaAlpha = 0.30f; g_settings.pdDerivativeGain = 0.025f;
                                    g_settings.velocityLeadFactor = 0.30f; g_settings.velocityLeadClamp = 20.0f;
                                    g_settings.enableConvergenceDamping = true; g_settings.convergenceRadius = 22.0f;
                                    g_settings.maxLockMissFrames = 2; g_settings.targetSwitchDelayFrames = 5;
                                    g_settings.recoilCompensationEnabled = false;
                                    g_settings.aimFovRadius = 220.0f;
                                    settings->headOffset.store(0.15f, std::memory_order_relaxed);
                                    settingsDirty = true;
                                }
                                if (ImGui::Button("Balanced", ImVec2(150, 0))) {
                                    g_settings.aimMode = 0; g_settings.aimSpeed = 0.52f;
                                    g_settings.smoothness = 0.80f; g_settings.filterType = 1;
                                    g_settings.emaAlpha = 0.22f; g_settings.pdDerivativeGain = 0.042f;
                                    g_settings.velocityLeadFactor = 0.24f; g_settings.velocityLeadClamp = 16.0f;
                                    g_settings.enableConvergenceDamping = true; g_settings.convergenceRadius = 32.0f;
                                    g_settings.maxLockMissFrames = 2; g_settings.targetSwitchDelayFrames = 9;
                                    g_settings.recoilCompensationEnabled = false;
                                    g_settings.aimFovRadius = 260.0f;
                                    settings->headOffset.store(0.18f, std::memory_order_relaxed);
                                    settingsDirty = true;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button("Precision", ImVec2(150, 0))) {
                                    g_settings.aimMode = 2; g_settings.aimSpeed = 0.58f;
                                    g_settings.smoothness = 0.88f; g_settings.filterType = 2;
                                    g_settings.kalmanProcessNoise = 0.8f; g_settings.kalmanMeasurementNoise = 5.0f;
                                    g_settings.pdDerivativeGain = 0.030f; g_settings.velocityLeadFactor = 0.18f;
                                    g_settings.velocityLeadClamp = 14.0f;
                                    g_settings.enableConvergenceDamping = true; g_settings.convergenceRadius = 40.0f;
                                    g_settings.maxLockMissFrames = 2; g_settings.targetSwitchDelayFrames = 12;
                                    g_settings.recoilCompensationEnabled = false;
                                    g_settings.aimFovRadius = 300.0f;
                                    settings->headOffset.store(0.17f, std::memory_order_relaxed);
                                    settingsDirty = true;
                                }

                                ImGui::Separator();

                                float offset = settings->headOffset.load(std::memory_order_relaxed);
                                if (ImGui::SliderFloat("Head offset", &offset, 0.0f, 0.5f, "%.2f")) {
                                    settings->headOffset.store(offset, std::memory_order_relaxed);
                                    settingsDirty = true;
                                }
                                ShowSettingHelp("Adjusts vertical aim point inside the box. Increase to target higher.");

                                int priority = static_cast<int>(g_settings.targetPriority);
                                const char* priorities[] = { "Nearest", "Largest", "Confidence" };
                                if (ImGui::Combo("Target priority", &priority, priorities, 3)) {
                                    g_settings.targetPriority = priority;
                                    settingsDirty = true;
                                }

                                ImGui::Separator();

                                if (g_settings.aimFovRadius > settings->fovRadius.load(std::memory_order_relaxed)) {
                                    g_settings.aimFovRadius = settings->fovRadius.load(std::memory_order_relaxed);
                                }

                                const char* aimModes[] = { "Smooth", "Snap", "Magnetic" };
                                int aimMode = static_cast<int>(g_settings.aimMode);
                                if (ImGui::Combo("Aim mode", &aimMode, aimModes, 3)) { g_settings.aimMode = aimMode; settingsDirty = true; }

                                float aimSpeed = g_settings.aimSpeed;
                                if (ImGui::SliderFloat("Aim speed", &aimSpeed, 0.1f, 1.0f, "%.2f")) { g_settings.aimSpeed = QuantizeStep(aimSpeed, 0.01f); settingsDirty = true; }
                                ShowSettingHelp("Higher is faster snap movement; lower is slower and smoother.");

                                float smoothness = g_settings.smoothness;
                                if (ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f, "%.2f")) { g_settings.smoothness = QuantizeStep(smoothness, 0.01f); settingsDirty = true; }
                                ShowSettingHelp("Higher smoothness looks natural but reacts slower.");

                                float aimFov = g_settings.aimFovRadius;
                                if (ImGui::SliderFloat("Aim FOV", &aimFov, 50.0f, 600.0f, "%.0f px")) {
                                    g_settings.aimFovRadius = QuantizeStep(aimFov, 1.0f);
                                    if (g_settings.aimFovRadius > settings->fovRadius.load(std::memory_order_relaxed)) {
                                        g_settings.aimFovRadius = settings->fovRadius.load(std::memory_order_relaxed);
                                    }
                                    settingsDirty = true;
                                }
                                ShowSettingHelp("Only targets inside this radius can be selected.");

                                float maxDist = g_settings.maxAimDistance;
                                if (ImGui::SliderFloat("Max aim distance", &maxDist, 100.0f, 1000.0f, "%.0f px")) {
                                    g_settings.maxAimDistance = QuantizeStep(maxDist, 1.0f);
                                    settingsDirty = true;
                                }

                                int fps = static_cast<int>(g_settings.aimbotFps);
                                if (ImGui::SliderInt("Aimbot FPS", &fps, 30, 120)) {
                                    g_settings.aimbotFps = static_cast<uint32_t>(fps);
                                    settingsDirty = true;
                                }
                                ShowSettingHelp("Update rate for assist logic. Higher is more responsive but heavier.");

                                ImGui::Separator();

                                const char* filterTypes[] = { "None", "EMA", "Kalman" };
                                int filterType = static_cast<int>(g_settings.filterType);
                                if (ImGui::Combo("Stabilization filter", &filterType, filterTypes, 3)) {
                                    g_settings.filterType = filterType;
                                    settingsDirty = true;
                                }
                                if (g_settings.filterType == 1) {
                                    float ema = g_settings.emaAlpha;
                                    if (ImGui::SliderFloat("EMA alpha", &ema, 0.1f, 0.9f, "%.2f")) { g_settings.emaAlpha = QuantizeStep(ema, 0.01f); settingsDirty = true; }
                                } else if (g_settings.filterType == 2) {
                                    float pn = g_settings.kalmanProcessNoise;
                                    if (ImGui::SliderFloat("Kalman process", &pn, 0.1f, 5.0f, "%.1f")) { g_settings.kalmanProcessNoise = QuantizeStep(pn, 0.1f); settingsDirty = true; }
                                    float mn = g_settings.kalmanMeasurementNoise;
                                    if (ImGui::SliderFloat("Kalman measure", &mn, 1.0f, 10.0f, "%.1f")) { g_settings.kalmanMeasurementNoise = QuantizeStep(mn, 0.1f); settingsDirty = true; }
                                }

                                bool antiOvershoot = g_settings.enableConvergenceDamping;
                                if (ImGui::Checkbox("Anti overshoot", &antiOvershoot)) { g_settings.enableConvergenceDamping = antiOvershoot; settingsDirty = true; }
                                if (g_settings.enableConvergenceDamping) {
                                    float cr = g_settings.convergenceRadius;
                                    if (ImGui::SliderFloat("Damp radius", &cr, 10.0f, 100.0f, "%.0f px")) { g_settings.convergenceRadius = QuantizeStep(cr, 1.0f); settingsDirty = true; }
                                }

                                float pdGain = g_settings.pdDerivativeGain;
                                if (ImGui::SliderFloat("Derivative damping", &pdGain, 0.0f, 0.12f, "%.3f")) { g_settings.pdDerivativeGain = QuantizeStep(pdGain, 0.005f); settingsDirty = true; }

                                float leadFactor = g_settings.velocityLeadFactor;
                                if (ImGui::SliderFloat("Velocity lead", &leadFactor, 0.0f, 0.20f, "%.2f")) { g_settings.velocityLeadFactor = QuantizeStep(leadFactor, 0.01f); settingsDirty = true; }
                                float leadClamp = g_settings.velocityLeadClamp;
                                if (ImGui::SliderFloat("Lead clamp", &leadClamp, 1.0f, 40.0f, "%.0f px")) { g_settings.velocityLeadClamp = QuantizeStep(leadClamp, 1.0f); settingsDirty = true; }

                                bool recoilOn = g_settings.recoilCompensationEnabled;
                                if (ImGui::Checkbox("Recoil compensation", &recoilOn)) { g_settings.recoilCompensationEnabled = recoilOn; settingsDirty = true; }
                                if (g_settings.recoilCompensationEnabled) {
                                    float rs = g_settings.recoilCompensationStrength;
                                    if (ImGui::SliderFloat("Recoil strength", &rs, 0.0f, 0.35f, "%.2f")) { g_settings.recoilCompensationStrength = QuantizeStep(rs, 0.01f); settingsDirty = true; }
                                    float rm = g_settings.recoilCompensationMax;
                                    if (ImGui::SliderFloat("Recoil max", &rm, 2.0f, 18.0f, "%.0f px")) { g_settings.recoilCompensationMax = QuantizeStep(rm, 1.0f); settingsDirty = true; }
                                    float rd = g_settings.recoilCompensationDecay;
                                    if (ImGui::SliderFloat("Recoil decay", &rd, 0.50f, 0.98f, "%.2f")) { g_settings.recoilCompensationDecay = QuantizeStep(rd, 0.01f); settingsDirty = true; }
                                }

                                int missFrames = g_settings.maxLockMissFrames;
                                if (ImGui::SliderInt("Miss grace", &missFrames, 1, 12, "%d")) { g_settings.maxLockMissFrames = missFrames; settingsDirty = true; }
                                int switchDelay = g_settings.targetSwitchDelayFrames;
                                if (ImGui::SliderInt("Switch delay", &switchDelay, 0, 20, "%d")) { g_settings.targetSwitchDelayFrames = switchDelay; settingsDirty = true; }

                                ImGui::Separator();

                                float tcx = settings->touchCenterX.load(std::memory_order_relaxed);
                                if (ImGui::SliderFloat("Touch center X", &tcx, 0.5f, 0.95f, "%.2f")) { settings->touchCenterX.store(tcx, std::memory_order_relaxed); settingsDirty = true; }
                                float tcy = settings->touchCenterY.load(std::memory_order_relaxed);
                                if (ImGui::SliderFloat("Touch center Y", &tcy, 0.3f, 0.7f, "%.2f")) { settings->touchCenterY.store(tcy, std::memory_order_relaxed); settingsDirty = true; }
                                float tr = settings->touchRadius.load(std::memory_order_relaxed);
                                if (ImGui::SliderFloat("Touch radius", &tr, 50.0f, 300.0f, "%.0f px")) { settings->touchRadius.store(tr, std::memory_order_relaxed); settingsDirty = true; }
                                float ad = settings->aimDelay.load(std::memory_order_relaxed);
                                if (ImGui::SliderFloat("Aim delay", &ad, 0.0f, 5.0f, "%.1f ms")) { settings->aimDelay.store(ad, std::memory_order_relaxed); settingsDirty = true; }

                                if (ImGui::Button("Reset touch zone", ImVec2(220.0f, 0.0f))) {
                                    settings->touchCenterX.store(0.75f, std::memory_order_relaxed);
                                    settings->touchCenterY.store(0.5f, std::memory_order_relaxed);
                                    settings->touchRadius.store(150.0f, std::memory_order_relaxed);
                                    settings->aimDelay.store(0.0f, std::memory_order_relaxed);
                                    settingsDirty = true;
                                }
                            }
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Info")) {
                        ImGui::Text("AimBuddy (open source)");
                        ImGui::Text("Version: 0.1.0-beta.1");
                        ImGui::Text("Overlay FPS: %.0f", g_measuredOverlayFps);
                        ImGui::Text("Inference: %.1f ms", g_measuredInferenceMs);
                        ImGui::Text("Detections: %d", static_cast<int>(latest.boxes.size()));
                        ImGui::Text("Screen: %dx%d", g_screenWidth, g_screenHeight);
                        ImGui::TextDisabled("github.com/1337XCode/AimBuddy");
                        ImGui::Separator();
                        ImGui::TextWrapped("Tip: use presets first, then fine tune only a few controls for stable behavior.");
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }

                ImGui::EndChild();

                if (ImGui::Button("Save now", ImVec2(160.0f, 0.0f))) {
                    ApplyRenderConfigToUnifiedSettings(*settings);
                    g_settings.validate();
                    g_settings.save();
                    g_settingsPendingSave = false;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Changes auto-save shortly after edits");
            }
            if (settingsDirty) {
                ApplyRenderConfigToUnifiedSettings(*settings);
                g_settingsPendingSave = true;
                g_settingsDirtyAt = ImGui::GetTime();
            }
            ImGui::End();
            g_menuWasVisible = true;
        } else {
            g_menuWasVisible = false;
        }

        if (g_settingsPendingSave) {
            const double now = ImGui::GetTime();
            if ((now - g_settingsDirtyAt) >= SETTINGS_SAVE_DELAY_SEC && !ImGui::IsAnyItemActive()) {
                g_settings.validate();
                g_settings.save();
                g_settingsPendingSave = false;
            }
        }

        // Render ImGui
        ImGui::Render();
        
        // Clear to transparent
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Render ImGui draw data
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    } catch (const std::exception& e) {
        LOGE("Exception in nativeImGuiRender: %s", e.what());
    } catch (...) {
        LOGE("Unknown exception in nativeImGuiRender");
    }
}

// Handle touch events
extern "C" JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeMotionEvent(
    JNIEnv* /* env */, jclass /* this */,
    jint action, jfloat x, jfloat y) {
    
    if (!g_imguiInitialized) {
        return JNI_FALSE;
    }

    ImGuiIO& io = ImGui::GetIO();

    // If menu is visible, feed ImGui input
    if (g_menuVisible) {
        // Update mouse position
        io.AddMousePosEvent(x, y);

        switch (action) {
            case 0: // ACTION_DOWN
                io.AddMouseButtonEvent(0, true);
                break;
            case 1: // ACTION_UP
                io.AddMouseButtonEvent(0, false);
                break;
            case 2: // ACTION_MOVE
                break;
            default:
                return JNI_FALSE;
        }

        // Consume input while menu is visible
        return JNI_TRUE;
    }

    // Pass-through to game
    return JNI_FALSE;
}

// Expose whether ImGui wants to capture touch
extern "C" JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeWantsCapture(JNIEnv* /* env */, jclass /* this */) {
    if (!g_imguiInitialized) {
        return JNI_FALSE;
    }
    ImGuiIO& io = ImGui::GetIO();
    return (g_menuVisible || io.WantCaptureMouse) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeSetMenuVisible(JNIEnv* /* env */, jclass /* this */, jboolean visible) {
    ESP::RenderConfig* settings = GetRenderConfig();
    if (settings) {
        settings->menuVisible.store(visible == JNI_TRUE, std::memory_order_relaxed);
    }
    g_menuVisible = (visible == JNI_TRUE);
}


// Shutdown ImGui
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeShutdown(JNIEnv* /* env */, jclass /* this */) {
    if (!g_imguiInitialized) {
        return;
    }

    LOGI("Shutting down ImGui menu");
    
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    
    if (g_menuWindow) {
        ANativeWindow_release(g_menuWindow);
        g_menuWindow = nullptr;
    }
    
    g_imguiInitialized = false;
    LOGI("ImGui menu shutdown complete");
}

// Set icon position from Kotlin layer (for menu positioning)
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeSetIconPosition(JNIEnv* /* env */, jclass /* this */, jfloat x, jfloat y) {
    g_iconPos.x = x + (ICON_RADIUS * 0.5f);  // Adjust to center of icon
    g_iconPos.y = y + (ICON_RADIUS * 0.5f);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeSetRootAvailable(JNIEnv* /* env */, jclass /* this */, jboolean available) {
    g_rootAvailable.store(available == JNI_TRUE, std::memory_order_relaxed);
    LOGI("Root status updated: %s", available ? "AVAILABLE" : "NOT AVAILABLE");
}
