/**
 * imgui_menu.cpp - The overlay: draws the targets and the settings menu with ImGui and hands the
 * touches that land on the menu to it.
 *
 * Kotlin owns the window (ImGuiGLSurface) and calls the functions at the bottom of this file. The
 * window passes every touch through to the game except while the menu is open; a tap anywhere
 * outside the menu closes it, so the overlay can never keep blocking the screen.
 */

#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "app_state.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_android.h"
#include "imgui/imgui_impl_opengl3.h"
#include "overlay/target_overlay.h"
#include "utils/i18n.h"
#include "utils/logger.h"

using aimbuddy::Settings;
using aimbuddy::i18n::Key;
using aimbuddy::i18n::T;

extern "C" bool GetDetectionZoneScreenRect(float* left, float* top, float* right, float* bottom);
extern "C" void NotifyStreamerModeChanged(bool enabled);

namespace {

// How far ahead of "now" targets are drawn. A frame drawn now reaches the screen about one to
// two refreshes later, so targets are projected that far to land on the right spot.
constexpr int64_t kDisplayLeadNs = 20'000'000;

// Overlay frame caps. The menu may render faster for smoother slider feedback.
constexpr int kBaseFpsCap = 60;
constexpr int kMenuFpsCap = 90;

// Settings are written this long after the last change, once no control is being dragged.
constexpr double kSaveDelaySeconds = 0.35;

// Size of the floating icon the menu opens beside, in pixels.
constexpr float kIconRadius = 44.0f;

ANativeWindow* g_window = nullptr;
bool g_imguiReady = false;
int g_windowWidth = 0;
int g_windowHeight = 0;

std::atomic<bool> g_menuVisible{false};
std::atomic<uint64_t> g_frameCount{0};

// Where the overlay window's top-left corner sits on the display. Game space drawing (targets, the
// crosshair, the detection zone) is in display pixels and is shifted by this.
std::atomic<int> g_overlayOriginX{0};
std::atomic<int> g_overlayOriginY{0};

// Where the floating icon is, so the menu can open beside it.
ImVec2 g_iconPosition = ImVec2(60.0f, 200.0f);

// The menu's rectangle (left, top, right, bottom) as drawn last, to tell a tap on it from one outside it.
std::atomic<float> g_menuRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};

ImVec2 g_menuSize = ImVec2(0.0f, 0.0f);
bool g_menuWasVisible = false;
bool g_streamerModeApplied = false;
std::chrono::steady_clock::time_point g_lastTick{};
float g_measuredFps = 0.0f;

// ---------------------------------------------------------------------------------------------
// Fonts and theme
// ---------------------------------------------------------------------------------------------

/// Reads a file into memory that the font atlas will own, or returns nullptr.
unsigned char* readFile(const char* path, size_t* size) {
    FILE* file = std::fopen(path, "rb");
    if (!file) {
        return nullptr;
    }
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length <= 0) {
        std::fclose(file);
        return nullptr;
    }
    unsigned char* buffer = static_cast<unsigned char*>(IM_ALLOC(static_cast<size_t>(length)));
    const size_t got = buffer ? std::fread(buffer, 1, static_cast<size_t>(length), file) : 0;
    std::fclose(file);
    if (got != static_cast<size_t>(length)) {
        IM_FREE(buffer);
        return nullptr;
    }
    *size = static_cast<size_t>(length);
    return buffer;
}

/// Adds the first readable font among `paths` to the atlas, for the glyphs in `ranges`. `merge` puts them in the previous font.
bool addFont(const char* const* paths, int count, float pixelSize, const ImWchar* ranges, bool merge) {
    ImGuiIO& io = ImGui::GetIO();
    for (int i = 0; i < count; ++i) {
        size_t size = 0;
        unsigned char* data = readFile(paths[i], &size);
        if (!data) {
            continue;
        }
        ImFontConfig config;
        config.FontDataOwnedByAtlas = true;
        config.MergeMode = merge;
        config.PixelSnapH = true;
        if (io.Fonts->AddFontFromMemoryTTF(data, static_cast<int>(size), pixelSize, &config, ranges)) {
            LOGI("Menu font: %s", paths[i]);
            return true;
        }
    }
    return false;
}

/**
 * Builds the menu's font: a Latin and Cyrillic base with Chinese added where needed. Every phone
 * ships fonts for all of these, so none has to be bundled.
 */
void loadFonts(float pixelSize) {
    ImGuiIO& io = ImGui::GetIO();

    static const char* const kBaseFonts[] = {
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/RobotoStatic-Regular.ttf",
        "/system/fonts/NotoSans-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
    };
    static const char* const kChineseFonts[] = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/NotoSerifCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/NotoSansSC-Regular.otf",
        "/system/fonts/MiSans-Regular.ttf",
        "/system/fonts/HarmonyOS_Sans_SC.ttf",
        "/product/fonts/NotoSansCJK-Regular.ttc",
    };

    static ImVector<ImWchar> baseRanges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    builder.BuildRanges(&baseRanges);

    if (!addFont(kBaseFonts, 4, pixelSize, baseRanges.Data, false)) {
        io.Fonts->AddFontDefault();   // Latin only, but always there
        LOGW("No system font with Latin and Cyrillic glyphs; using ImGui's built-in font");
    }
    if (!addFont(kChineseFonts, 7, pixelSize, io.Fonts->GetGlyphRangesChineseSimplifiedCommon(), true)) {
        LOGI("No Chinese font on this device; Chinese text will show as boxes");
    }
}

void applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.97f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.9f, 0.2f, 0.3f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.95f, 0.3f, 0.4f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.6f, 0.45f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.2f, 0.7f, 0.55f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.9f, 0.3f, 0.4f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.2f, 0.9f, 0.5f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.3f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.4f, 1.0f);

    // Scale for fingers rather than a mouse.
    style.ScaleAllSizes(1.15f);
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 8.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.WindowBorderSize = 1.0f;
    style.ScrollbarSize = 18.0f;
}

// ---------------------------------------------------------------------------------------------
// The game overlay: crosshair, targets, zones
// ---------------------------------------------------------------------------------------------

/// Draws a line, a box or a marker for every target, and the zones the detector and the assist work in.
void drawGameOverlay(const Settings& settings, const ImVec2& origin, int* bodyCountOut) {
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    const aimbuddy::CaptureGeometry geometry = aimbuddy::pipeline().capture().geometry();

    // The game, the detector and the aim logic all use display pixels, whose centre is where the
    // in-game crosshair is. The overlay window may be smaller than the display or offset from it.
    const float displayWidth = geometry.isValid() ? static_cast<float>(geometry.screenWidth) : windowSize.x;
    const float displayHeight = geometry.isValid() ? static_cast<float>(geometry.screenHeight) : windowSize.y;
    const ImVec2 crosshair(displayWidth * 0.5f - origin.x, displayHeight * 0.5f - origin.y);

    const float arm = 11.5f;
    const ImU32 crossColor = IM_COL32(58, 156, 255, 240);
    list->AddLine(ImVec2(crosshair.x - arm, crosshair.y), ImVec2(crosshair.x + arm, crosshair.y), crossColor, 3.0f);
    list->AddLine(ImVec2(crosshair.x, crosshair.y - arm), ImVec2(crosshair.x, crosshair.y + arm), crossColor, 3.0f);

    // Targets come from the tracker, projected to the moment this frame will be on screen.
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now().time_since_epoch()).count();
    aimbuddy::TrackView views[aimbuddy::kMaxTracks];
    const int viewCount = aimbuddy::pipeline().tracker().snapshot(nowNs + kDisplayLeadNs, views, aimbuddy::kMaxTracks);
    const aimbuddy::ModelProfile& profile = aimbuddy::pipeline().profile();

    aimbuddy::TargetStyle style;
    style.color = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.boxColor[0], settings.boxColor[1], settings.boxColor[2], 1.0f));
    style.thickness = static_cast<float>(settings.boxThickness);
    style.showScore = settings.showLabels;
    style.showAimPoint = settings.showAimPoint;
    style.aimZone = static_cast<aimbuddy::AimZone>(settings.aimZone);
    style.lockedTrackId = aimbuddy::aimLoop().stats().lockedTrack;
    style.origin = origin;
    aimbuddy::TargetOverlay::draw(list, views, viewCount, profile, style);

    int bodies = 0;
    const aimbuddy::TrackView* nearest = nullptr;
    float nearestDistanceSq = 0.0f;
    for (int i = 0; i < viewCount; ++i) {
        if (profile.roleOf(views[i].classId) != aimbuddy::ClassRole::kBody) {
            continue;
        }
        ++bodies;
        const float dx = views[i].centerX - displayWidth * 0.5f;
        const float dy = views[i].centerY - displayHeight * 0.5f;
        const float distanceSq = dx * dx + dy * dy;
        if (nearest == nullptr || distanceSq < nearestDistanceSq) {
            nearest = &views[i];
            nearestDistanceSq = distanceSq;
        }
    }
    *bodyCountOut = bodies;

    if (settings.showSnapLine && nearest != nullptr) {
        list->AddLine(crosshair, ImVec2(nearest->centerX - origin.x, nearest->centerY - origin.y),
                      IM_COL32(255, 100, 50, static_cast<int>(255.0f * nearest->opacity)), 2.0f);
    }

    // The detection zone (blue box) and, while the assist is on, the radius it picks targets from (red circle).
    float left, top, right, bottom;
    if (GetDetectionZoneScreenRect(&left, &top, &right, &bottom)) {
        list->AddRect(ImVec2(left - origin.x, top - origin.y), ImVec2(right - origin.x, bottom - origin.y),
                      IM_COL32(40, 140, 255, 220), 0.0f, 0, 2.2f);
        if (settings.assistEnabled) {
            list->AddCircle(crosshair, settings.aimFovRadius, IM_COL32(255, 60, 60, 230), 64, 2.3f);
        }
    }

    // Where the assist's finger rests, so the player can keep their own thumb away from it.
    if (settings.assistEnabled && settings.showAnchor) {
        const ImVec2 anchor(settings.anchorX * displayWidth - origin.x, settings.anchorY * displayHeight - origin.y);
        const int alpha = static_cast<int>(255.0f * settings.anchorOpacity);
        list->AddCircleFilled(anchor, settings.anchorRadius, IM_COL32(120, 200, 255, alpha / 4), 64);
        list->AddCircle(anchor, settings.anchorRadius, IM_COL32(120, 200, 255, alpha), 64, 2.0f);
    }

    if (settings.showCount && bodies > 0) {
        char text[64];
        std::snprintf(text, sizeof(text), "%d", bodies);
        ImFont* font = ImGui::GetFont();
        const float size = ImGui::GetFontSize() * 2.0f;
        const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        list->AddText(font, size, ImVec2((windowSize.x - textSize.x) * 0.5f, 40.0f), IM_COL32(255, 50, 50, 255), text);
    }
}

// ---------------------------------------------------------------------------------------------
// The menu
// ---------------------------------------------------------------------------------------------

/// A short explanation that appears when the "(?)" after a control is held.
void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/// What to tell the player about the touch helper.
const char* helperStatusText(const aimbuddy::AimLoop::Stats& stats) {
    using aimbuddy::touch::protocol::Reason;
    using aimbuddy::touch::protocol::State;
    const char* reason = nullptr;
    switch (stats.helperReason) {
        case Reason::kNoTouchscreen: reason = T(Key::ReasonNoTouchscreen); break;
        case Reason::kTouchscreenDenied: reason = T(Key::ReasonDenied); break;
        case Reason::kVirtualDeviceDenied: reason = T(Key::ReasonVirtualDenied); break;
        case Reason::kGrabFailed: reason = T(Key::ReasonGrab); break;
        case Reason::kHeartbeatLost: reason = T(Key::ReasonHeartbeat); break;
        case Reason::kEscapeKeys: reason = T(Key::ReasonEscape); break;
        default: break;
    }
    if (!stats.helperConnected) {
        return reason != nullptr ? reason : T(Key::HelperOff);
    }
    switch (stats.helperState) {
        case State::kMerging: return T(Key::HelperActive);
        case State::kWaitingForFingers: return T(Key::HelperWaiting);
        default: return reason != nullptr ? reason : T(Key::HelperIdle);
    }
}

bool drawVisualsTab(Settings& s) {
    bool changed = false;
    changed |= ImGui::Checkbox(T(Key::ShowLabels), &s.showLabels);
    changed |= ImGui::Checkbox(T(Key::ShowSnapLine), &s.showSnapLine);
    changed |= ImGui::Checkbox(T(Key::ShowAimPoint), &s.showAimPoint);
    changed |= ImGui::Checkbox(T(Key::ShowCount), &s.showCount);
    changed |= ImGui::Checkbox(T(Key::ShowPerformance), &s.showPerformance);
    ImGui::Separator();

    changed |= ImGui::ColorEdit3(T(Key::BoxColor), s.boxColor, ImGuiColorEditFlags_NoInputs);
    changed |= ImGui::SliderInt(T(Key::BoxThickness), &s.boxThickness, 1, 6);
    ImGui::Separator();

    changed |= ImGui::SliderFloat(T(Key::Confidence), &s.confidence, 0.1f, 0.95f, "%.2f");
    changed |= ImGui::SliderFloat(T(Key::DetectionZone), &s.detectionZoneRadius, 100.0f, 650.0f, "%.0f px");
    ImGui::Separator();

    changed |= ImGui::Checkbox(T(Key::ShowAnchor), &s.showAnchor);
    if (s.showAnchor) {
        changed |= ImGui::SliderFloat(T(Key::AnchorOpacity), &s.anchorOpacity, 0.1f, 1.0f, "%.2f");
    }
    ImGui::Separator();

    changed |= ImGui::Checkbox(T(Key::StreamerMode), &s.streamerMode);
    const char* languages[] = {aimbuddy::i18n::LanguageName(0), aimbuddy::i18n::LanguageName(1),
                               aimbuddy::i18n::LanguageName(2)};
    if (ImGui::Combo(T(Key::Language), &s.language, languages, 3)) {
        aimbuddy::i18n::SetLanguage(s.language);
        changed = true;
    }
    return changed;
}

bool drawAimTab(Settings& s) {
    bool changed = false;
    changed |= ImGui::Checkbox(T(Key::AssistEnable), &s.assistEnabled);

    const aimbuddy::AimLoop::Stats stats = aimbuddy::aimLoop().stats();
    ImGui::TextWrapped("%s", helperStatusText(stats));
    ImGui::Separator();

    const char* access[] = {T(Key::AccessAutomatic), T(Key::AccessShizuku), T(Key::AccessRoot)};
    changed |= ImGui::Combo(T(Key::TouchAccess), &s.touchBackend, access, 3);

    const char* zones[] = {T(Key::ZoneHead), T(Key::ZoneChest), T(Key::ZoneCentre)};
    changed |= ImGui::Combo(T(Key::AimZone), &s.aimZone, zones, 3);
    changed |= ImGui::SliderFloat(T(Key::AimStrength), &s.aimStrength, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat(T(Key::AimRadius), &s.aimFovRadius, 50.0f, 650.0f, "%.0f px");
    const char* priorities[] = {T(Key::PriorityNearest), T(Key::PriorityLargest), T(Key::PriorityCertain)};
    changed |= ImGui::Combo(T(Key::AimPriority), &s.aimPriority, priorities, 3);

    ImGui::SeparatorText(T(Key::CalibrationTitle));
    ImGui::TextWrapped("%s", T(Key::CalibrationHint));
    changed |= ImGui::SliderFloat(T(Key::CameraSensitivity), &s.sceneGain, 0.2f, 6.0f, "%.2f");
    changed |= ImGui::SliderFloat(T(Key::ResponseDelay), &s.actuationDelayMs, 0.0f, 200.0f, "%.0f ms");
    changed |= ImGui::Checkbox(T(Key::InvertVertical), &s.invertY);

    ImGui::SeparatorText(T(Key::TouchAreaTitle));
    changed |= ImGui::SliderFloat(T(Key::AnchorX), &s.anchorX, 0.5f, 0.95f, "%.2f");
    changed |= ImGui::SliderFloat(T(Key::AnchorY), &s.anchorY, 0.2f, 0.8f, "%.2f");
    changed |= ImGui::SliderFloat(T(Key::AnchorSize), &s.anchorRadius, 50.0f, 400.0f, "%.0f px");
    if (ImGui::Button(T(Key::ResetTouchArea))) {
        const Settings defaults;
        s.anchorX = defaults.anchorX;
        s.anchorY = defaults.anchorY;
        s.anchorRadius = defaults.anchorRadius;
        changed = true;
    }
    return changed;
}

void drawInfoTab(int bodyCount) {
    const aimbuddy::DetectionPipeline::Stats stats = aimbuddy::pipeline().stats();
    ImGui::TextUnformatted(T(Key::InfoTitle));
    ImGui::TextUnformatted(T(Key::InfoVersion));
    ImGui::Text("%s: %.0f", T(Key::InfoOverlayFps), g_measuredFps);
    ImGui::Text("%s: %.0f", T(Key::InfoDetectionsPerSecond), stats.detectionsPerSecond);
    ImGui::Text("%s: %.0f ms (%s)", T(Key::InfoDelay), stats.frameAgeMs, stats.backend);
    ImGui::Text("%s: %d", T(Key::InfoTargets), bodyCount);
    ImGui::Text("%s: %dx%d", T(Key::InfoScreen), g_windowWidth, g_windowHeight);
    ImGui::TextDisabled("github.com/1337XCode/AimBuddy");
    ImGui::Separator();
    ImGui::TextWrapped("%s", T(Key::InfoTip));
}

/// Draws the menu beside the floating icon. Returns true if a setting changed.
bool drawMenu(Settings& settings, int bodyCount) {
    const ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    if (!g_menuWasVisible || g_menuSize.x <= 0.0f || g_menuSize.y <= 0.0f) {
        g_menuSize = ImVec2(std::max(460.0f, std::min(windowSize.x * 0.48f, 860.0f)),
                            std::max(560.0f, std::min(windowSize.y * 0.92f, 1060.0f)));
    }
    float x = g_iconPosition.x + kIconRadius + 52.0f;
    float y = g_iconPosition.y - g_menuSize.y * 0.5f;
    if (x + g_menuSize.x > windowSize.x) {
        x = g_iconPosition.x - kIconRadius - 52.0f - g_menuSize.x;
    }
    x = std::max(4.0f, std::min(x, windowSize.x - g_menuSize.x - 4.0f));
    y = std::max(4.0f, std::min(y, windowSize.y - g_menuSize.y - 4.0f));

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(g_menuSize, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 460.0f), ImVec2(windowSize.x - 8.0f, windowSize.y - 8.0f));

    bool changed = false;
    bool open = true;
    if (ImGui::Begin(T(Key::AppTitle), &open, ImGuiWindowFlags_NoCollapse)) {
        g_menuSize = ImGui::GetWindowSize();
        const ImVec2 position = ImGui::GetWindowPos();
        g_menuRect[0].store(position.x, std::memory_order_relaxed);
        g_menuRect[1].store(position.y, std::memory_order_relaxed);
        g_menuRect[2].store(position.x + g_menuSize.x, std::memory_order_relaxed);
        g_menuRect[3].store(position.y + g_menuSize.y, std::memory_order_relaxed);

        ImGui::BeginChild("##MenuScroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 6.0f), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        if (ImGui::BeginTabBar("##MenuTabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            if (ImGui::BeginTabItem(T(Key::TabVisuals))) {
                changed |= drawVisualsTab(settings);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(T(Key::TabAim))) {
                changed |= drawAimTab(settings);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(T(Key::TabInfo))) {
                drawInfoTab(bodyCount);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        if (ImGui::Button(T(Key::Close), ImVec2(160.0f, 0.0f))) {
            open = false;
        }
    }
    ImGui::End();
    if (!open) {
        g_menuVisible.store(false, std::memory_order_relaxed);
    }
    return changed;
}

/// Saves the settings once they have stopped changing.
void saveSettingsWhenDue() {
    int64_t changedAtNs = 0;
    if (!aimbuddy::settingsStore().hasUnsavedChanges(&changedAtNs) || ImGui::IsAnyItemActive()) {
        return;
    }
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now().time_since_epoch()).count();
    if (static_cast<double>(nowNs - changedAtNs) * 1e-9 >= kSaveDelaySeconds) {
        aimbuddy::settingsStore().saveIfChanged(aimbuddy::settingsPath());
    }
}

/// Keeps the overlay from drawing faster than needed, and measures how fast it does draw.
void limitFrameRate() {
    const int cap = g_menuVisible.load(std::memory_order_relaxed) ? kMenuFpsCap : kBaseFpsCap;
    const auto minFrameTime = std::chrono::nanoseconds(1'000'000'000LL / cap);
    if (g_lastTick.time_since_epoch().count() != 0) {
        const auto elapsed = std::chrono::steady_clock::now() - g_lastTick;
        if (elapsed < minFrameTime) {
            std::this_thread::sleep_for(minFrameTime - elapsed);
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (g_lastTick.time_since_epoch().count() != 0) {
        const float seconds = std::chrono::duration<float>(now - g_lastTick).count();
        if (seconds > 0.0f && seconds <= 0.25f) {
            const float instant = 1.0f / seconds;
            g_measuredFps = g_measuredFps > 0.0f ? g_measuredFps * 0.9f + instant * 0.1f : instant;
        }
    }
    g_lastTick = now;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Calls from ImGuiGLSurface
// ---------------------------------------------------------------------------------------------

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeInit(JNIEnv* env, jclass /* clazz */, jobject /* assetManager */, jobject surface) {
    if (g_imguiReady) {
        LOGI("ImGui already initialised");
        return;
    }
    if (!surface) {
        LOGE("Surface is null");
        return;
    }
    g_window = ANativeWindow_fromSurface(env, surface);
    if (!g_window) {
        LOGE("Failed to get ANativeWindow from Surface");
        return;
    }
    g_windowWidth = ANativeWindow_getWidth(g_window);
    g_windowHeight = ANativeWindow_getHeight(g_window);
    LOGI("Overlay window: %dx%d", g_windowWidth, g_windowHeight);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.DisplaySize = ImVec2(static_cast<float>(g_windowWidth), static_cast<float>(g_windowHeight));

    loadFonts(20.0f);   // before the backend, which builds the atlas
    applyTheme();
    aimbuddy::i18n::SetLanguage(aimbuddy::settingsStore().get().language);

    ImGui_ImplAndroid_Init(g_window);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_imguiReady = true;
    LOGI("ImGui initialised");
}

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeSurfaceChanged(JNIEnv* /* env */, jclass /* clazz */, jint width, jint height) {
    if (!g_imguiReady) {
        return;
    }
    g_windowWidth = width;
    g_windowHeight = height;
    glViewport(0, 0, width, height);
    // The overlay is not the screen: it shrinks while the navigation bar is showing. Detection and
    // aiming always work in display pixels (see NativeBridge.configureDisplay).
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
}

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeSetOrigin(JNIEnv* /* env */, jclass /* clazz */, jint x, jint y) {
    g_overlayOriginX.store(x, std::memory_order_relaxed);
    g_overlayOriginY.store(y, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeTick(JNIEnv* /* env */, jclass /* clazz */) {
    if (!g_imguiReady || !g_window) {
        return;
    }
    try {
        limitFrameRate();
        g_frameCount.fetch_add(1, std::memory_order_relaxed);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplAndroid_NewFrame();
        ImGui::NewFrame();

        const Settings settings = aimbuddy::settingsStore().get();
        aimbuddy::i18n::SetLanguage(settings.language);

        const ImVec2 origin(static_cast<float>(g_overlayOriginX.load(std::memory_order_relaxed)),
                            static_cast<float>(g_overlayOriginY.load(std::memory_order_relaxed)));
        int bodyCount = 0;
        drawGameOverlay(settings, origin, &bodyCount);

        if (g_menuVisible.load(std::memory_order_relaxed)) {
            Settings edited = settings;
            if (drawMenu(edited, bodyCount)) {
                aimbuddy::settingsStore().set(edited);
            }
            g_menuWasVisible = true;
        } else {
            g_menuWasVisible = false;
            for (std::atomic<float>& side : g_menuRect) {
                side.store(0.0f, std::memory_order_relaxed);
            }
        }
        saveSettingsWhenDue();

        if (settings.streamerMode != g_streamerModeApplied) {
            g_streamerModeApplied = settings.streamerMode;
            NotifyStreamerModeChanged(g_streamerModeApplied);
        }

        ImGui::Render();
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    } catch (const std::exception& e) {
        LOGE("Exception while drawing the overlay: %s", e.what());
    } catch (...) {
        LOGE("Unknown exception while drawing the overlay");
    }
}

/**
 * Hands a touch to the menu. Returns true if the touch was used. While the menu is open the whole
 * overlay is touchable, so a touch that lands outside the menu closes it: the player can always get
 * back to the game with one tap, even if the menu failed to draw.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeMotionEvent(JNIEnv* /* env */, jclass /* clazz */, jint action, jfloat x, jfloat y) {
    if (!g_imguiReady || !g_menuVisible.load(std::memory_order_relaxed)) {
        return JNI_FALSE;
    }
    if (action == 0) {
        const bool insideMenu = x >= g_menuRect[0].load(std::memory_order_relaxed) &&
                                y >= g_menuRect[1].load(std::memory_order_relaxed) &&
                                x <= g_menuRect[2].load(std::memory_order_relaxed) &&
                                y <= g_menuRect[3].load(std::memory_order_relaxed);
        if (!insideMenu) {
            g_menuVisible.store(false, std::memory_order_relaxed);
            return JNI_TRUE;
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    if (action == 0) {
        io.AddMouseButtonEvent(0, true);
    } else if (action == 1) {
        io.AddMouseButtonEvent(0, false);
    }
    return JNI_TRUE;
}

/// Whether the overlay window should be touchable: only while the menu is open.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeWantsCapture(JNIEnv* /* env */, jclass /* clazz */) {
    return g_menuVisible.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeSetMenuVisible(JNIEnv* /* env */, jclass /* clazz */, jboolean visible) {
    g_menuVisible.store(visible == JNI_TRUE, std::memory_order_relaxed);
}

/// How many frames the overlay has drawn, so Kotlin can tell a stuck renderer from a working one.
extern "C" JNIEXPORT jlong JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeFrameCount(JNIEnv* /* env */, jclass /* clazz */) {
    return static_cast<jlong>(g_frameCount.load(std::memory_order_relaxed));
}

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeSetIconPosition(JNIEnv* /* env */, jclass /* clazz */, jfloat x, jfloat y) {
    g_iconPosition.x = x + kIconRadius * 0.5f;
    g_iconPosition.y = y + kIconRadius * 0.5f;
}

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_ImGuiGLSurface_nativeShutdown(JNIEnv* /* env */, jclass /* clazz */) {
    if (!g_imguiReady) {
        return;
    }
    LOGI("Shutting down the overlay");
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    if (g_window) {
        ANativeWindow_release(g_window);
        g_window = nullptr;
    }
    g_menuVisible.store(false, std::memory_order_relaxed);
    g_imguiReady = false;
}
