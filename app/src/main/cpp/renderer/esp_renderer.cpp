#include "esp_renderer.h"
#include "../utils/vector2.h"
#include "../utils/imgui_helper.h"
#include "../utils/aimbot_types.h"
#include "../utils/detection_zone.h"
#include "../aimbot/aimbot_controller.h"
#include <algorithm>

// Forward declare accessor function
extern "C" AimbotController* GetAimbotController();
extern "C" void GetCaptureSize(int* outWidth, int* outHeight);

// External reference to global unified settings
extern UnifiedSettings g_settings;

namespace ESP {

ESPRenderer::ESPRenderer()
    : window_(nullptr)
    , initialized_(false)
    , menuButtonHovered_(false) {
    LOGD("ESPRenderer created");
}

ESPRenderer::~ESPRenderer() {
    shutdown();
}

bool ESPRenderer::initialize(OverlayWindow* window) {
    if (initialized_) {
        LOGW("ESPRenderer already initialized");
        return true;
    }
    
    if (!window || !window->isInitialized()) {
        LOGE("OverlayWindow not initialized");
        return false;
    }
    
    window_ = window;
    
    LOGI("Initializing ESPRenderer");
    
    // Ensure no existing context
    if (ImGui::GetCurrentContext() != nullptr) {
        LOGW("ImGui context already exists, destroying old context");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
    }
    
    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    
    // Configure ImGui for touch input and Android
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(static_cast<float>(window_->getWidth()), 
                            static_cast<float>(window_->getHeight()));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    
    // Disable saving imgui.ini
    io.IniFilename = nullptr;
    
    // Setup style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.Alpha = 0.9f;
    
    // Initialize platform/renderer backends in correct order
    // Must initialize Android platform backend first
    ImGui_ImplAndroid_Init(window_->getNativeWindow());
    // Then OpenGL3 renderer backend
    ImGui_ImplOpenGL3_Init("#version 300 es");
    
    // Create font atlas and build
    ImFontAtlas* fontAtlas = io.Fonts;
    fontAtlas->Clear();
    fontAtlas->AddFontDefault();
    
    // Upload fonts to GPU
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    fontAtlas->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0) {
        LOGE("Failed to build font atlas");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    
    initialized_ = true;
    LOGI("ESPRenderer initialized successfully");
    return true;
}

void ESPRenderer::shutdown() {
    if (!initialized_) {
        return;
    }
    
    LOGI("Shutting down ESPRenderer");
    
    // Cleanup ImGui in reverse order of initialization
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts) {
        io.Fonts->Clear();
    }
    
    // Shutdown renderer backend
    ImGui_ImplOpenGL3_Shutdown();
    
    // Shutdown platform backend
    ImGui_ImplAndroid_Shutdown();
    
    // Destroy ImGui context
    ImGui::DestroyContext();
    
    window_ = nullptr;
    initialized_ = false;
    
    LOGI("ESPRenderer shutdown complete");
}

void ESPRenderer::render(const DetectionResult& result) {
    if (!initialized_ || !window_ || !window_->isInitialized()) {
        LOGE("Renderer not properly initialized");
        return;
    }
    
    timer_.tick();
    
    // Make sure OpenGL context is active
    if (!window_->makeCurrent()) {
        LOGE("Failed to make GL context current");
        return;
    }
    
    // Clear with transparent background
    window_->clear();
    
    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    
    // Update DisplaySize (critical for orientation/resizing)
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(window_->getWidth()), 
                           static_cast<float>(window_->getHeight()));
    
    ImGui::NewFrame();
    
    // Apply dynamic scaling based on screen height
    // Reference scale for 2400px height device (common mobile)
    float scaleFactor = window_->getHeight() / 2400.0f;
    scaleFactor = std::max(0.5f, std::min(2.0f, scaleFactor)); // Clamp to reasonable range
    io.FontGlobalScale = scaleFactor;
    
    // Apply smoothing if enabled (reduces jitter)
    bool smoothingEnabled = g_settings.enableSmoothing;

    if (smoothingEnabled) {
        if (result.boxes.size() > 0) {
            float alpha = g_settings.smoothingFactor;

            // Create mutable copy for smoother
            std::array<BoundingBox, Config::MAX_DETECTIONS> inputBoxes;
            std::copy(result.boxes.begin(), result.boxes.end(), inputBoxes.begin());

            boxSmoother_.update(inputBoxes, result.boxes.size(), smoothedBoxes_, smoothedCount_, alpha);

            // Construct smoothed result
            DetectionResult smoothedResult;
            for (int i = 0; i < smoothedCount_; i++) {
                smoothedResult.boxes.push(smoothedBoxes_[i]);
            }

            drawESPBoxes(smoothedResult);

            if (g_settings.showFPS || g_settings.showDetectionCount) {
                drawInfoOverlay(smoothedResult);
            }
        } else {
            // No detections at all — flush the smoother immediately so ghost boxes
            // don't persist. Even with MAX_AGE=2 the box would still show for 2 frames;
            // flushing guarantees instant removal.
            boxSmoother_.clear();
            smoothedCount_ = 0;

            // Still draw the crosshair / zone overlay (handled by drawESPBoxes with empty result)
            drawESPBoxes(result);

            if (g_settings.showFPS || g_settings.showDetectionCount) {
                drawInfoOverlay(result);
            }
        }
    } else {
        // Zero-copy path: strict pass-through of raw results
        drawESPBoxes(result);
        
        if (g_settings.showFPS || 
            g_settings.showDetectionCount) {
            drawInfoOverlay(result);
        }
    }
    
    // Draw touch zone overlay if enabled
    if (g_settings.showTouchZone) {
        drawTouchZoneOverlay();
    }
    
    // Render ImGui draw data to screen
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    // Swap buffers
    if (!window_->swapBuffers()) {
        LOGW("Failed to swap buffers");
    }
}

bool ESPRenderer::handleTouch(int /* action */, float /* x */, float /* y */) {
    // Menu handled by GLSurfaceView overlay. Always pass through.
    return false;
}

void ESPRenderer::drawESPBoxes(const DetectionResult& result) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y;
    
    // Draw screen center crosshair
    float centerX = screenW * 0.5f;
    float centerY = screenH * 0.5f;
    float crossSize = 11.5f;
    ImU32 crosshairColor = IM_COL32(58, 156, 255, 240);
    drawList->AddLine(ImVec2(centerX - crossSize, centerY), ImVec2(centerX + crossSize, centerY), crosshairColor, 3.0f);
    drawList->AddLine(ImVec2(centerX, centerY - crossSize), ImVec2(centerX, centerY + crossSize), crosshairColor, 3.0f);
    
    const float espFovRadius = g_settings.fovRadius;
    const float aimFovRadius = std::min(
        (g_settings.aimFovRadius > 0.0f) ? g_settings.aimFovRadius : g_settings.fovRadius,
        espFovRadius
    );

    int captureWidth = Config::CAPTURE_WIDTH;
    int captureHeight = Config::CAPTURE_HEIGHT;
    GetCaptureSize(&captureWidth, &captureHeight);
    const int logicalScreenWidth = (g_settings.screenWidth > 0)
        ? g_settings.screenWidth
        : static_cast<int>(screenW);
    const ESP::DetectionZoneMetrics zone = ESP::ComputeDetectionZoneMetrics(
        espFovRadius,
        logicalScreenWidth,
        screenW,
        screenH,
        captureWidth,
        captureHeight
    );

    const ImU32 espFovColor = IM_COL32(40, 140, 255, 210);
    const ImVec2 zoneTl(centerX - zone.halfWidthPx, centerY - zone.halfHeightPx);
    const ImVec2 zoneBr(centerX + zone.halfWidthPx, centerY + zone.halfHeightPx);
    ESP::ImGuiHelper::DrawBox3D(drawList, zoneTl, zoneBr, espFovColor, 2.2f, IM_COL32(10, 20, 40, 140));
    ESP::ImGuiHelper::DrawBoxCorners(drawList, zoneTl, zoneBr, espFovColor, 2.4f, 16.0f);

    if (g_settings.aimbotEnabled) {
        drawList->AddCircle(ImVec2(centerX, centerY), aimFovRadius, IM_COL32(255, 60, 60, 220), 48, 2.2f);
    }
    
    int count = result.boxes.size();
    if (count == 0) {
        return;
    }
    
    // Cache config (read once)
    int thickness = g_settings.boxThickness;
    float threshold = g_settings.confidenceThreshold;
    bool showLabels = g_settings.showLabels;
    bool drawLine = g_settings.drawLine;
    bool drawDot = g_settings.drawDot;
    
    thickness = std::max(1, std::min(thickness, 5));
    float thicknessF = static_cast<float>(thickness);
    
    ImU32 shadowColor = IM_COL32(0, 0, 0, 120);
    
    // Pre-define class colors
    ImU32 enemyColor = IM_COL32(Config::ENEMY_BOX_COLOR_R, Config::ENEMY_BOX_COLOR_G, Config::ENEMY_BOX_COLOR_B, 255);
    ImU32 selfColor = IM_COL32(Config::SELF_BOX_COLOR_R, Config::SELF_BOX_COLOR_G, Config::SELF_BOX_COLOR_B, 255);
    ImU32 teammateColor = IM_COL32(Config::TEAMMATE_BOX_COLOR_R, Config::TEAMMATE_BOX_COLOR_G, Config::TEAMMATE_BOX_COLOR_B, 255);
    
    // Reserve capacity (Optimized: ~100 idx per box)
    drawList->PrimReserve(count * 100, count * 200);
    
    // Track closest enemy for targeting line
    int closestEnemyIdx = -1;
    float closestEnemyDistSq = 1.0e30f;
    
    // Helper lambda to draw a single box
    auto drawBox = [&](const BoundingBox& box, int index) {
        // Box corners (top-left format)
        float left = box.x;
        float top = box.y;
        float right = box.x + box.width;
        float bottom = box.y + box.height;
        
        // Select color based on class ID
        ImU32 boxColor = enemyColor;
        const char* label = "ENEMY";
        
        switch (box.classId) {
            case Config::ENEMY_CLASS_ID:
                boxColor = enemyColor;
                label = "ENEMY";
                break;
            case Config::SELF_CLASS_ID:
                boxColor = selfColor;
                label = "SELF";
                break;
            case Config::TEAMMATE_CLASS_ID:
                boxColor = teammateColor;
                label = "TEAM";
                break;
        }
        
        // High confidence = brighter, low confidence = dimmer
        ImU32 finalColor = (box.confidence > 0.6f) ? boxColor : 
                          IM_COL32(
                              (boxColor & 0xFF) * 0.7f,
                              ((boxColor >> 8) & 0xFF) * 0.7f,
                              ((boxColor >> 16) & 0xFF) * 0.7f,
                              217
                          );
        
        ESP::ImGuiHelper::DrawBox3D(
            drawList,
            ImVec2(left, top),
            ImVec2(right, bottom),
            finalColor,
            thicknessF,
            shadowColor
        );
        
        // Head/Center Dot
        float boxCenterX = left + box.width * 0.5f;
        float boxCenterY = top + box.height * 0.5f;
        
        if (drawDot) {
             drawList->AddCircleFilled(ImVec2(boxCenterX, boxCenterY), 5.0f, finalColor, 12);
        }
        
        // Closest Logic
        if (box.classId == Config::ENEMY_CLASS_ID) {
            float distSq = (boxCenterX - centerX) * (boxCenterX - centerX) + 
                          (boxCenterY - centerY) * (boxCenterY - centerY);
            if (distSq < closestEnemyDistSq) {
                closestEnemyDistSq = distSq;
                closestEnemyIdx = index;
            }
        }
        
        // Labels
        if (showLabels) {
            char labelText[32];
            snprintf(labelText, sizeof(labelText), "%s %.0f%%", label, box.confidence * 100.0f);
            ImVec2 textPos(left, top - 20.0f);
            if (textPos.y < 0.0f) textPos.y = bottom + 2.0f;
            drawList->AddText(ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0,0,0,180), labelText);
            drawList->AddText(textPos, finalColor, labelText);
        }
    };
    
    for (int i = 0; i < count; ++i) {
        const BoundingBox& box = result.boxes[i];
        if (box.confidence < threshold || box.width <= 1.0f || box.height <= 1.0f) continue;
        if (box.x + box.width < 0.0f || box.x > screenW || box.y + box.height < 0.0f || box.y > screenH) continue;
        
        drawBox(box, i);
    }
    
    // Draw Snapline
    if (drawLine && closestEnemyIdx >= 0) {
        const BoundingBox& enemyBox = result.boxes[closestEnemyIdx];
        float boxCenterX = enemyBox.x + enemyBox.width * 0.5f;
        
        // Snap to head if aimbot is relevant, otherwise center? 
        // Let's snap to Head to match Aimbot behavior
        float headOffset = g_settings.headOffset;
        float headY = enemyBox.y + enemyBox.height * headOffset;
        
        // float boxCenterY = enemyBox.y + enemyBox.height * 0.5f;
        
        drawList->AddLine(
            ImVec2(centerX, centerY),
            ImVec2(boxCenterX, headY),
            IM_COL32(255, 100, 50, 255), 2.0f
        );
    }
}

void ESPRenderer::drawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Scale menu size based on screen dimensions
    float scale = io.DisplaySize.y / 2400.0f;  // Reference scale
    float menuWidth = 450.0f * scale;  // Increased width for aimbot settings
    float menuHeight = 800.0f * scale;  // Increased height for scrolling
    
    // Center menu on screen
    ImVec2 menuSize(menuWidth, menuHeight);
    ImVec2 menuPos((io.DisplaySize.x - menuSize.x) * 0.5f,
                   (io.DisplaySize.y - menuSize.y) * 0.5f);
    
    ImGui::SetNextWindowPos(menuPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(menuSize, ImGuiCond_Always);
    
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | 
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoMove;
    
    if (ImGui::Begin("##ESP_Settings", nullptr, windowFlags)) {
        // Title
        ImGui::TextUnformatted("ESP Configuration");
        ImGui::Separator();
        
        // Color adjustment section
        ImGui::TextUnformatted("Box Color");
        float r = g_settings.boxColorR;
        float g = g_settings.boxColorG;
        float b = g_settings.boxColorB;
        
        if (ImGui::SliderFloat("##Red", &r, 0.0f, 1.0f)) {
            g_settings.boxColorR = r;
        }
        if (ImGui::SliderFloat("##Green", &g, 0.0f, 1.0f)) {
            g_settings.boxColorG = g;
        }
        if (ImGui::SliderFloat("##Blue", &b, 0.0f, 1.0f)) {
            g_settings.boxColorB = b;
        }
        
        // Color preview
        ImGui::Spacing();
        ImGui::TextUnformatted("Color Preview:");
        ImGui::SameLine();
        ImGui::ColorButton("##Preview", ImVec4(r, g, b, 1.0f), 
                          ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker, 
                          ImVec2(100.0f, 30.0f));
        
        ImGui::Separator();
        
        // Box thickness
        int thickness = g_settings.boxThickness;
        if (ImGui::SliderInt("Box Thickness", &thickness, 1, 5)) {
            g_settings.boxThickness = std::max(1, std::min(thickness, 5));
        }
        
        ImGui::Separator();
        
        // Confidence threshold
        float threshold = g_settings.confidenceThreshold;
        if (ImGui::SliderFloat("Confidence Threshold", &threshold, 0.1f, 0.95f, "%.2f")) {
            g_settings.confidenceThreshold = threshold;
        }
        
        // Detection FOV Radius (dynamic crop size optimization)
        if (float fovRadius = g_settings.fovRadius;
            ImGui::SliderFloat("Detection FOV", &fovRadius, 200.0f, 600.0f, "%.0f px")) {
            g_settings.fovRadius = fovRadius;
            if (g_settings.aimFovRadius > g_settings.fovRadius) {
                g_settings.aimFovRadius = g_settings.fovRadius;
            }
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), 
            "Smaller = faster (experimental)");
        
        ImGui::Separator();
        
        // Display toggles
        ImGui::TextUnformatted("Display Options");
        bool showFPS = g_settings.showFPS;
        if (ImGui::Checkbox("Show FPS", &showFPS)) {
            g_settings.showFPS = showFPS;
        }
        
        bool showCount = g_settings.showDetectionCount;
        if (ImGui::Checkbox("Show Detection Count", &showCount)) {
            g_settings.showDetectionCount = showCount;
        }

        bool showLabels = g_settings.showLabels;
        if (ImGui::Checkbox("Show Labels", &showLabels)) {
            g_settings.showLabels = showLabels;
        }
        
        ImGui::Separator();
        
        // ========== AIMBOT SETTINGS ==========
        ImGui::TextUnformatted("Aimbot Settings (Adaptive Algorithm)");
        
        // Master enable
        if (ImGui::Checkbox("Enable Aimbot", &g_settings.aimbotEnabled)) {
            // Toggle aimbot
        }
        
        // Aim Speed
        if (ImGui::SliderFloat("Aim Speed", &g_settings.aimSpeed, 0.1f, 1.0f, "%.2f")) {
            // Auto-clamped
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), 
            "Higher = faster aim movement");
        
        // Smoothness
        if (ImGui::SliderFloat("Smoothness", &g_settings.smoothness, 0.0f, 1.0f, "%.2f")) {
            // Auto-clamped
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), 
            "Higher = smoother easing curve");
        
        // Aim FOV Radius
        if (ImGui::SliderFloat("Aim FOV Radius", &g_settings.aimFovRadius, 50.0f, g_settings.fovRadius, "%.0f px")) {
            g_settings.validate();
        }
        
        // Target Priority
        const char* priorities[] = {"Closest to Crosshair", "Distance", "Confidence"};
        int currentPriority = g_settings.targetPriority;
        if (ImGui::Combo("Target Priority", &currentPriority, priorities, 3)) {
            g_settings.targetPriority = currentPriority;
        }
        
        // Head Priority
        if (ImGui::Checkbox("Aim at Head", &g_settings.headPriority)) {
            // Toggle
        }
        
        // Touch Configuration
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "Touch Zone (Template Logic)");
        
        // Show/hide touch zone overlay
        if (ImGui::Checkbox("Show Touch Zone Overlay", &g_settings.showTouchZone)) {
            // Toggle
        }
        
        if (g_settings.showTouchZone) {
            // Touch zone opacity
            if (ImGui::SliderFloat("Overlay Opacity", &g_settings.touchZoneAlpha, 0.1f, 1.0f, "%.2f")) {
                // Updated
            }
        }
        
        if (ImGui::SliderFloat("Touch X", &g_settings.touchX, 0.0f, (float)g_settings.screenWidth, "%.0f")) {
            // Updated
        }
        
        if (ImGui::SliderFloat("Touch Y", &g_settings.touchY, 0.0f, (float)g_settings.screenHeight, "%.0f")) {
            // Updated
        }
        
        if (ImGui::SliderFloat("Touch Radius", &g_settings.touchRadius, 50.0f, 500.0f, "%.0f")) {
            g_settings.validate();
        }
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), 
            "Max drag distance before reset");
        
        // Button to reset touch zone to default
        if (ImGui::Button("Reset Touch Zone Position", ImVec2(-1.0f, 0.0f))) {
            g_settings.setDefaultTouchPosition(g_settings.screenWidth, g_settings.screenHeight);
            LOGI("Touch zone reset to default position");
        }
        
        // Aim Delay
        if (ImGui::SliderFloat("Aim Delay (ms)", &g_settings.aimDelay, 0.0f, 50.0f, "%.1f")) {
            g_settings.validate();
        }
        
        // FPS Control
        ImGui::Spacing();
        int fpsi = (int)g_settings.aimbotFps;
        if (ImGui::SliderInt("Aimbot FPS", &fpsi, 30, 120)) {
            g_settings.aimbotFps = (uint32_t)fpsi;
        }
        
        // Save/Load Buttons
        ImGui::Spacing();
        if (ImGui::Button("Save Settings", ImVec2(-1.0f, 0.0f))) {
            if (g_settings.save()) {
                LOGI("Settings saved successfully");
            } else {
                LOGE("Failed to save settings");
            }
        }
        
        if (ImGui::Button("Load Settings", ImVec2(-1.0f, 0.0f))) {
            if (g_settings.load()) {
                LOGI("Settings loaded successfully");
                g_settings.validate();
            } else {
                LOGE("Failed to load settings");
            }
        }
        
        ImGui::Separator();
        
        // Smoothing settings
        ImGui::TextUnformatted("ESP Smoothing");
        bool enableSmoothing = g_settings.enableSmoothing;
        if (ImGui::Checkbox("Enable Box Smoothing", &enableSmoothing)) {
            g_settings.enableSmoothing = enableSmoothing;
        }
        
        if (enableSmoothing) {
            float smoothFactor = g_settings.smoothingFactor;
            if (ImGui::SliderFloat("Smoothing Factor", &smoothFactor, 0.1f, 1.0f, "%.2f")) {
                g_settings.smoothingFactor = smoothFactor;
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), 
                "Lower = smoother, Higher = faster");
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        
        // Close button (takes full width)
        if (ImGui::Button("Close Menu", ImVec2(-1.0f, 40.0f))) {
            config_.menuVisible.store(false, std::memory_order_relaxed);
        }
    }
    ImGui::End();
}

void ESPRenderer::drawMenuButton() {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    
    float buttonX = static_cast<float>(window_->getWidth()) - Config::MENU_BUTTON_OFFSET_X;
    float buttonY = Config::MENU_BUTTON_OFFSET_Y;
    float buttonRadius = Config::MENU_BUTTON_SIZE * 0.5f;
    
    // Button background - brighter colors for visibility
    ImU32 bgColor = config_.menuVisible.load(std::memory_order_relaxed) 
                    ? IM_COL32(0, 255, 100, 255)   // Bright green when menu open
                    : IM_COL32(100, 150, 255, 255); // Bright blue when menu closed
    
    drawList->AddCircleFilled(ImVec2(buttonX, buttonY), buttonRadius, bgColor);
    drawList->AddCircle(ImVec2(buttonX, buttonY), buttonRadius, IM_COL32_WHITE, 0, 2.0f);
    
    // Button icon (hamburger menu or X)
    if (config_.menuVisible.load(std::memory_order_relaxed)) {
        // X icon
        float iconSize = buttonRadius * 0.5f;
        drawList->AddLine(
            ImVec2(buttonX - iconSize, buttonY - iconSize),
            ImVec2(buttonX + iconSize, buttonY + iconSize),
            IM_COL32_WHITE, 3.0f);
        drawList->AddLine(
            ImVec2(buttonX + iconSize, buttonY - iconSize),
            ImVec2(buttonX - iconSize, buttonY + iconSize),
            IM_COL32_WHITE, 3.0f);
    } else {
        // Hamburger icon
        float iconWidth = buttonRadius * 0.6f;
        float lineSpacing = buttonRadius * 0.25f;
        
        for (int i = -1; i <= 1; ++i) {
            float y = buttonY + i * lineSpacing;
            drawList->AddLine(
                ImVec2(buttonX - iconWidth, y),
                ImVec2(buttonX + iconWidth, y),
                IM_COL32_WHITE, 3.0f);
        }
    }
}

void ESPRenderer::drawInfoOverlay(const DetectionResult& result) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    
    float x = 16.0f;
    float y = 16.0f;
    float lineHeight = 18.0f;
    
    ImU32 textColor = IM_COL32(255, 255, 255, 180);
    ImU32 enemyTextColor = IM_COL32(255, 100, 100, 220);
    char text[64];
    
    if (g_settings.showFPS) {
        // Fast float to string
        int fps = static_cast<int>(timer_.getFPS() + 0.5f);
        snprintf(text, sizeof(text), "FPS %d", fps);
        drawList->AddText(ImVec2(x, y), textColor, text);
        y += lineHeight;
    }
    
    if (g_settings.showDetectionCount) {
        // Count enemies separately (only enemies matter for targeting)
        int enemyCount = 0;
        
        // Optimization: Don't loop if no detections
        int count = result.boxes.size();
        if (count > 0) {
            for (int i = 0; i < count; ++i) {
                if (result.boxes[i].classId == Config::ENEMY_CLASS_ID) {
                    enemyCount++;
                }
            }
        }
        
        // Show enemy count prominently
        snprintf(text, sizeof(text), "Enemies: %d (%d)", enemyCount, count);
        drawList->AddText(ImVec2(x, y), enemyTextColor, text);
        y += lineHeight;
    }
}

void ESPRenderer::drawTouchZoneOverlay() {
    ImDrawList *drawList = ImGui::GetBackgroundDrawList();
    ImGuiIO &io = ImGui::GetIO();

    // Get touch zone position and size
    float touchX = g_settings.touchX;
    float touchY = g_settings.touchY;
    float radius = g_settings.touchRadius;
    float alpha = g_settings.touchZoneAlpha;

    // Clamp to screen bounds
    if (touchX < 0 || touchX > io.DisplaySize.x ||
        touchY < 0 || touchY > io.DisplaySize.y) {
        // Reset to default if out of bounds
        g_settings.setDefaultTouchPosition(g_settings.screenWidth, g_settings.screenHeight);
        touchX = g_settings.touchX;
        touchY = g_settings.touchY;
    }

    // Draw outer circle  (touch radius zone)
    ImU32 outerColor = IM_COL32(255, 165, 0, (int) (alpha * 255)); // Orange with alpha
    drawList->AddCircle(ImVec2(touchX, touchY), radius, outerColor, 64, 2.0f);

    // Draw filled inner circle (touch center point)
    ImU32 innerColor = IM_COL32(255, 100, 0,
                                (int) (alpha * 255 * 1.5f)); // Darker orange, more opaque
    drawList->AddCircleFilled(ImVec2(touchX, touchY), 10.0f, innerColor, 32);

    // Draw crosshair at center
    ImU32 crosshairColor = IM_COL32(255, 255, 255, (int) (alpha * 255));
    drawList->AddLine(
            ImVec2(touchX - 15.0f, touchY),
            ImVec2(touchX + 15.0f, touchY),
            crosshairColor, 2.0f
    );
    drawList->AddLine(
            ImVec2(touchX, touchY - 15.0f),
            ImVec2(touchX, touchY + 15.0f),
            crosshairColor, 2.0f
    );

    // Draw label
    char labelText[64];
    snprintf(labelText, sizeof(labelText), "Touch Zone (%.0f, %.0f) R:%.0f", touchX, touchY,
             radius);
    ImVec2 textSize = ImGui::CalcTextSize(labelText);
    float labelX = touchX - textSize.x * 0.5f;
    float labelY = touchY + radius + 10.0f;

    // Draw text background
    ImU32 bgColor = IM_COL32(0, 0, 0, (int) (alpha * 200));
    drawList->AddRectFilled(
            ImVec2(labelX - 4.0f, labelY - 2.0f),
            ImVec2(labelX + textSize.x + 4.0f, labelY + textSize.y + 2.0f),
            bgColor,
            4.0f
    );

    // Draw text
    ImU32 textColor = IM_COL32(255, 255, 255, (int) (alpha * 255));
    drawList->AddText(ImVec2(labelX, labelY), textColor, labelText);

    // Make touch zone draggable via invisible ImGui window
    ImGui::SetNextWindowPos(ImVec2(touchX - radius, touchY - radius));
    ImGui::SetNextWindowSize(ImVec2(radius * 2.0f, radius * 2.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    bool opened = true;
    if (ImGui::Begin("##TouchZoneDragArea", &opened, flags)) {
        // Check if window is being dragged
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(0)) {
            ImVec2 drag = ImGui::GetMouseDragDelta(0);
            g_settings.touchX = std::max(0.0f, std::min(touchX + drag.x, io.DisplaySize.x));
            g_settings.touchY = std::max(0.0f, std::min(touchY + drag.y, io.DisplaySize.y));
            ImGui::ResetMouseDragDelta(0);
        }
    }
    ImGui::End();
}

} // namespace ESP
