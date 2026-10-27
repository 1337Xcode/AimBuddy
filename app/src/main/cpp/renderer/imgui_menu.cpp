/**
 * imgui_menu.cpp - The overlay: draws the targets and the settings menu with ImGui and hands the
 * touches that land on the menu to it.
 *
 * Kotlin owns the window and the OpenGL context (OverlayView) and calls the functions at the
 * bottom of this file, the drawing ones always from the overlay's GL thread. The window passes
 * every touch through to the game except while the menu is open; a tap anywhere outside the menu
 * closes it, so the overlay can never keep blocking the screen.
 */

#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <thread>

#include "app_state.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "overlay/target_overlay.h"
#include "utils/i18n.h"
#include "utils/logger.h"

using aimbuddy::Settings;
using aimbuddy::i18n::Key;
using aimbuddy::i18n::T;

extern "C" bool GetDetectionZoneScreenRect(float* left, float* top, float* right, float* bottom);
extern "C" void NotifyStreamerModeChanged(bool enabled);
extern "C" void NotifyStopRequested();

namespace {

// How far ahead of "now" targets are drawn. A frame drawn now reaches the screen about one to
// two refreshes later, so targets are projected that far to land on the right spot.
constexpr int64_t kDisplayLeadNs = 20'000'000;

// Overlay frame caps. The menu may render faster for smoother slider feedback.
constexpr int kBaseFpsCap = 60;
constexpr int kMenuFpsCap = 90;

// Settings are written this long after the last change, once no control is being dragged.
constexpr double kSaveDelaySeconds = 0.35;

// The two fields of view drawn around the crosshair: a blue square for the area the detector looks
// at, and a red circle for how far the assist reaches.
constexpr float kFovLineWidth = 2.2f;
constexpr float kFovGapPx = 2.0f;   // between the blue square and the area the detector reads
constexpr ImU32 kDetectionFovColor = IM_COL32(40, 140, 255, 220);
constexpr ImU32 kAimFovColor = IM_COL32(255, 60, 60, 230);
constexpr ImU32 kAimFovIdleColor = IM_COL32(255, 60, 60, 150);   // while the assist is off


bool g_contextCreated = false;      ///< The ImGui context, fonts and theme exist (made once, kept for the process's life).
bool g_backendReady = false;        ///< The OpenGL backend is set up for the current GL context (GL thread only).
std::atomic<bool> g_imguiReady{false};   ///< Drawing may start: a GL context is attached.
int g_windowWidth = 0;
int g_windowHeight = 0;

std::atomic<bool> g_menuVisible{false};
std::atomic<bool> g_zoneEditing{false};   ///< The touch area editor is open. Like the menu, it makes the window take touches.
std::atomic<uint64_t> g_frameCount{0};

// Where the overlay window's top-left corner sits on the display. Game space drawing (targets, the
// crosshair, the detection zone) is in display pixels and is shifted by this.
std::atomic<int> g_overlayOriginX{0};
std::atomic<int> g_overlayOriginY{0};

// Where the floating icon is, so the menu can open beside it. Set from the UI thread.
std::atomic<float> g_iconX{60.0f};
std::atomic<float> g_iconY{200.0f};

// The menu's rectangle (left, top, right, bottom) as drawn last, to tell a tap on it from one outside it.
// Only the GL thread reads or writes it.
float g_menuRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};

ImVec2 g_menuSize = ImVec2(0.0f, 0.0f);
bool g_menuWasVisible = false;
bool g_streamerModeApplied = false;
std::chrono::steady_clock::time_point g_lastTick{};
float g_measuredFps = 0.0f;

// ---------------------------------------------------------------------------------------------
// Fonts and theme
// ---------------------------------------------------------------------------------------------

// Pixels per dp. The menu is laid out in dp, so it is the same physical size on every phone.
float g_dp = 2.0f;

float dp(float value) { return value * g_dp; }

// Size of the menu's text, in dp.
constexpr float kFontDp = 15.0f;

/// Reads a file that ships inside the app into memory that the font atlas will own, or returns nullptr.
unsigned char* readAsset(AAssetManager* assets, const char* name, size_t* size) {
    AAsset* asset = AAssetManager_open(assets, name, AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        return nullptr;
    }
    const off_t length = AAsset_getLength(asset);
    unsigned char* buffer = length > 0 ? static_cast<unsigned char*>(IM_ALLOC(static_cast<size_t>(length))) : nullptr;
    const int got = buffer != nullptr ? AAsset_read(asset, buffer, static_cast<size_t>(length)) : -1;
    AAsset_close(asset);
    if (buffer == nullptr || got != length) {
        IM_FREE(buffer);
        return nullptr;
    }
    *size = static_cast<size_t>(length);
    return buffer;
}

/// Adds a font that ships in the app to the atlas. `merge` puts its glyphs into the previous font. Returns false if it is missing.
bool addFont(AAssetManager* assets, const char* name, float pixelSize, bool merge) {
    size_t size = 0;
    unsigned char* data = readAsset(assets, name, &size);
    if (data == nullptr) {
        return false;
    }
    ImFontConfig config;
    config.FontDataOwnedByAtlas = true;
    config.MergeMode = merge;
    config.PixelSnapH = true;
    if (ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, static_cast<int>(size), pixelSize, &config) == nullptr) {
        IM_FREE(data);
        return false;
    }
    return true;
}

/**
 * Builds the menu's font from fonts that ship in the app: Geist for Latin and Cyrillic, and a small
 * Noto Sans SC with just the characters the Chinese texts use. They are bundled because the fonts
 * a phone carries differ by maker and by Android version, and some of them cannot be read by the
 * font engine (the system Chinese font on recent Android is one), which left the menu blank.
 * Glyphs are made when first drawn, so nothing is built for characters that are never shown.
 */
void loadFonts(AAssetManager* assets, float pixelSize) {
    if (!addFont(assets, "fonts/Geist-Regular.ttf", pixelSize, false)) {
        LOGW("The menu font is missing from the app; using ImGui's built-in font (Latin only)");
        ImGui::GetIO().Fonts->AddFontDefault();
        return;
    }
    if (!addFont(assets, "fonts/NotoSansSC-Subset.ttf", pixelSize, true)) {
        LOGW("The Chinese font is missing from the app; Chinese text will show as boxes");
    }
}

/// A colour from its 0xRRGGBB value and an opacity.
ImVec4 rgb(uint32_t value, float alpha = 1.0f) {
    return ImVec4(static_cast<float>((value >> 16) & 0xFF) / 255.0f, static_cast<float>((value >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(value & 0xFF) / 255.0f, alpha);
}

/// The menu's look: one dark surface, one blue accent, soft corners, and controls sized for a finger.
void applyTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);

    const ImVec4 background = rgb(0x0F1218, 0.98f);
    const ImVec4 surface = rgb(0x171C24);
    const ImVec4 raised = rgb(0x222935);
    const ImVec4 raisedHover = rgb(0x2B3444);
    const ImVec4 accent = rgb(0x6EA8FF);
    const ImVec4 accentSoft = rgb(0x6EA8FF, 0.28f);
    const ImVec4 text = rgb(0xE6EAF0);
    const ImVec4 textDim = rgb(0x8B95A5);
    const ImVec4 hairline = rgb(0xFFFFFF, 0.08f);
    const ImVec4 clear = rgb(0x000000, 0.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = textDim;
    colors[ImGuiCol_WindowBg] = background;
    colors[ImGuiCol_ChildBg] = clear;
    colors[ImGuiCol_PopupBg] = surface;
    colors[ImGuiCol_Border] = hairline;
    colors[ImGuiCol_FrameBg] = raised;
    colors[ImGuiCol_FrameBgHovered] = raisedHover;
    colors[ImGuiCol_FrameBgActive] = raisedHover;
    colors[ImGuiCol_TitleBg] = background;
    colors[ImGuiCol_TitleBgActive] = background;
    colors[ImGuiCol_TitleBgCollapsed] = background;
    colors[ImGuiCol_ScrollbarBg] = clear;
    colors[ImGuiCol_ScrollbarGrab] = raisedHover;
    colors[ImGuiCol_ScrollbarGrabHovered] = accentSoft;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = rgb(0x9DC2FF);
    colors[ImGuiCol_Button] = raised;
    colors[ImGuiCol_ButtonHovered] = raisedHover;
    colors[ImGuiCol_ButtonActive] = accentSoft;
    colors[ImGuiCol_Header] = raised;
    colors[ImGuiCol_HeaderHovered] = raisedHover;
    colors[ImGuiCol_HeaderActive] = accentSoft;
    colors[ImGuiCol_Separator] = hairline;
    colors[ImGuiCol_Tab] = clear;
    colors[ImGuiCol_TabHovered] = raisedHover;
    colors[ImGuiCol_TabSelected] = raised;
    colors[ImGuiCol_TabSelectedOverline] = accent;
    colors[ImGuiCol_TabDimmed] = clear;
    colors[ImGuiCol_TabDimmedSelected] = raised;
    colors[ImGuiCol_TabDimmedSelectedOverline] = accent;
    colors[ImGuiCol_ResizeGrip] = clear;
    colors[ImGuiCol_ResizeGripHovered] = accentSoft;
    colors[ImGuiCol_ResizeGripActive] = accent;

    style.WindowRounding = dp(14.0f);
    style.ChildRounding = dp(10.0f);
    style.PopupRounding = dp(12.0f);
    style.FrameRounding = dp(10.0f);
    style.GrabRounding = dp(10.0f);
    style.ScrollbarRounding = dp(10.0f);
    style.TabRounding = dp(10.0f);
    style.WindowPadding = ImVec2(dp(16.0f), dp(14.0f));
    style.FramePadding = ImVec2(dp(12.0f), dp(9.0f));
    style.ItemSpacing = ImVec2(dp(12.0f), dp(10.0f));
    style.ItemInnerSpacing = ImVec2(dp(8.0f), dp(6.0f));
    style.IndentSpacing = dp(16.0f);
    style.ScrollbarSize = dp(10.0f);
    style.GrabMinSize = dp(22.0f);
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBarBorderSize = 0.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
}

// ---------------------------------------------------------------------------------------------
// The game overlay: crosshair, targets, zones
// ---------------------------------------------------------------------------------------------

/// Where the performance label sits, in font heights: its distance from the right edge and from the
/// top (far enough down to clear the status bar), and how far its text is from the label's own edge.
constexpr float kReadoutMarginEm = 0.8f;
constexpr float kReadoutTopEm = 4.4f;
constexpr float kReadoutPaddingEm = 0.3f;

/// Draws how many detections finish each second, how old the picture is when its targets are ready,
/// and which processor runs the network, in a small label at the top right.
void drawPerformanceReadout(ImDrawList* list, const ImVec2& windowSize) {
    const aimbuddy::DetectionPipeline::Stats stats = aimbuddy::pipeline().stats();
    char text[64];
    const bool gpu = std::strncmp(stats.backend, "GPU", 3) == 0;
    std::snprintf(text, sizeof(text), "%.0f/s  %.0f ms  %s", stats.detectionsPerSecond, stats.frameAgeMs,
                  stats.backend[0] == '\0' ? "" : (gpu ? "GPU" : "CPU"));
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float padding = kReadoutPaddingEm * size;
    const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const ImVec2 corner(windowSize.x - kReadoutMarginEm * size - textSize.x - 2.0f * padding, kReadoutTopEm * size);
    list->AddRectFilled(corner, ImVec2(corner.x + textSize.x + 2.0f * padding, corner.y + textSize.y + 2.0f * padding),
                        IM_COL32(0, 0, 0, 150), 0.3f * size);
    list->AddText(font, size, ImVec2(corner.x + padding, corner.y + padding), IM_COL32(255, 255, 255, 230), text);
}

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

    // The guides (crosshair, detection square, reach circle) are for setting up. While playing they
    // would sit on top of the game and inside the picture the detector reads, so they only show while
    // the menu or the touch area editor is open.
    const bool showGuides = g_menuVisible.load(std::memory_order_relaxed) || g_zoneEditing.load(std::memory_order_relaxed);
    if (showGuides) {
        const float arm = 11.5f;
        const ImU32 crossColor = IM_COL32(58, 156, 255, 240);
        list->AddLine(ImVec2(crosshair.x - arm, crosshair.y), ImVec2(crosshair.x + arm, crosshair.y), crossColor, 3.0f);
        list->AddLine(ImVec2(crosshair.x, crosshair.y - arm), ImVec2(crosshair.x, crosshair.y + arm), crossColor, 3.0f);
    }

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

    // The two fields of view: the area the detector looks at, and how far the assist reaches. They
    // show while playing when their switches are on, and always while the menu or the touch area
    // editor is open, so that the sliders that set them have something to show.
    float left, top, right, bottom;
    if ((settings.showDetectionArea || showGuides) && GetDetectionZoneScreenRect(&left, &top, &right, &bottom)) {
        // Drawn just outside the area the detector reads, so its outline is never in the picture it looks at.
        const float margin = kFovLineWidth + kFovGapPx;
        list->AddRect(ImVec2(left - origin.x - margin, top - origin.y - margin),
                      ImVec2(right - origin.x + margin, bottom - origin.y + margin), kDetectionFovColor, 0.0f, 0,
                      kFovLineWidth);
    }
    if (settings.showAimReach || showGuides) {
        list->AddCircle(crosshair, settings.aimFovRadius, settings.assistEnabled ? kAimFovColor : kAimFovIdleColor, 64,
                        kFovLineWidth);
    }

    // Where the assist's finger rests, so the player can keep their own thumb away from it. The
    // editor draws the area itself while it is open.
    if (settings.assistEnabled && settings.showAnchor && !g_zoneEditing.load(std::memory_order_relaxed)) {
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

    if (settings.showPerformance) {
        drawPerformanceReadout(list, windowSize);
    }
}

// ---------------------------------------------------------------------------------------------
// Touch input
// ---------------------------------------------------------------------------------------------

/// One thing the finger did on the menu, kept until ImGui can be shown it.
struct TouchStep {
    enum class Kind { kMove, kPress, kRelease, kLeave };
    Kind kind;
    float x;
    float y;
};

// A press is held at least this many frames before it is released. A tap can be over in a few
// milliseconds, less than one frame, and ImGui only clicks what it has seen pressed during a frame.
constexpr int kMinPressFrames = 2;

std::deque<TouchStep> g_touchSteps;   ///< What the finger did, not yet shown to ImGui. GL thread only.
bool g_pressShown = false;            ///< ImGui has been told the button is down.
int g_framesPressed = 0;              ///< Frames the button has been down.

/**
 * Shows ImGui what the finger did, spaced so that even a very quick tap becomes a click.
 *
 * ImGui treats a screen like a mouse, but a mouse moves over a control for a while before it
 * clicks. A finger arrives and leaves within one frame, and then the click is lost. So the pointer
 * is moved first, the press follows on the next frame, and the press is held for at least
 * [kMinPressFrames] frames. Without this, quick taps on the menu were ignored.
 */
void feedTouchToImGui() {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    if (g_pressShown) {
        ++g_framesPressed;
    }
    bool moved = false;
    while (!g_touchSteps.empty()) {
        const TouchStep step = g_touchSteps.front();
        if (step.kind == TouchStep::Kind::kMove) {
            io.AddMousePosEvent(step.x, step.y);
            moved = true;
            g_touchSteps.pop_front();
            continue;
        }
        if (moved) {
            break;   // the pointer arrives on this frame; what follows it comes on the next
        }
        if (step.kind == TouchStep::Kind::kPress) {
            io.AddMouseButtonEvent(0, true);
            g_pressShown = true;
            g_framesPressed = 0;
        } else if (step.kind == TouchStep::Kind::kRelease) {
            if (g_pressShown && g_framesPressed < kMinPressFrames) {
                break;
            }
            io.AddMouseButtonEvent(0, false);
            g_pressShown = false;
        } else {
            if (g_pressShown) {
                break;
            }
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);   // the finger is gone: nothing stays highlighted
        }
        g_touchSteps.pop_front();
        break;   // a press, a release or a leave takes a frame of its own
    }
}

/// Keeps what a finger does on the menu for feedTouchToImGui() to show to ImGui, in order.
void queueImGuiDown(float x, float y) {
    g_touchSteps.push_back({TouchStep::Kind::kMove, x, y});
    g_touchSteps.push_back({TouchStep::Kind::kPress, x, y});
}

void queueImGuiMove(float x, float y) {
    // Several moves between two frames collapse into the last.
    if (!g_touchSteps.empty() && g_touchSteps.back().kind == TouchStep::Kind::kMove) {
        g_touchSteps.back() = {TouchStep::Kind::kMove, x, y};
    } else {
        g_touchSteps.push_back({TouchStep::Kind::kMove, x, y});
    }
}

void queueImGuiUp(float x, float y) {
    g_touchSteps.push_back({TouchStep::Kind::kRelease, x, y});
    g_touchSteps.push_back({TouchStep::Kind::kLeave, x, y});
}

/// Forgets the finger, so a press that was cut short (the menu closed) cannot stay down for next time.
void resetTouch() {
    if (!g_pressShown && g_touchSteps.empty()) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (g_pressShown) {
        io.AddMouseButtonEvent(0, false);
        g_pressShown = false;
    }
    g_touchSteps.clear();
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

// ---------------------------------------------------------------------------------------------
// The touch area editor
// ---------------------------------------------------------------------------------------------

// The touch area is the circle where the assist's finger rests. The editor lets the player drag it
// to move it and drag the dot on its rim to resize it. While it is open the window takes every
// touch and the assist is paused, so the player's finger and the assist's never meet.

constexpr float kZoneHandleSlope = 0.70710678f;   // the dot sits at the lower right of the circle

float zoneHandleRadius() { return dp(9.0f); }       // the dot on the rim
float zoneHandleGrabRadius() { return dp(26.0f); }  // how near a finger must land to take the dot

/// Half the width of the floating icon the menu opens beside, in pixels (the icon is 44 dp wide).
float iconHalf() { return dp(22.0f); }

/// What the finger that is down took hold of.
enum class Grab { kNone, kToolbar, kMove, kResize };

Grab g_grab = Grab::kNone;                           ///< GL thread only.
ImVec2 g_grabOffset = ImVec2(0.0f, 0.0f);            ///< From the finger to the area's centre while moving.
float g_toolbarRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};   ///< The editor's buttons as drawn last: left, top, right, bottom.

/// The size of the display the detector and the assist work in, in pixels.
ImVec2 displayExtent() {
    const aimbuddy::CaptureGeometry geometry = aimbuddy::pipeline().capture().geometry();
    if (geometry.isValid()) {
        return ImVec2(static_cast<float>(geometry.screenWidth), static_cast<float>(geometry.screenHeight));
    }
    return ImVec2(static_cast<float>(g_windowWidth), static_cast<float>(g_windowHeight));
}

/// Where the touch area is, in overlay window pixels.
struct ZoneGeometry {
    ImVec2 centre;
    float radius;
    ImVec2 handle;
};

ZoneGeometry zoneGeometry(const Settings& settings, const ImVec2& origin) {
    const ImVec2 extent = displayExtent();
    ZoneGeometry zone;
    zone.centre = ImVec2(settings.anchorX * extent.x - origin.x, settings.anchorY * extent.y - origin.y);
    zone.radius = settings.anchorRadius;
    zone.handle = ImVec2(zone.centre.x + zone.radius * kZoneHandleSlope, zone.centre.y + zone.radius * kZoneHandleSlope);
    return zone;
}

float distanceBetween(const ImVec2& a, const ImVec2& b) { return std::hypot(a.x - b.x, a.y - b.y); }

bool insideRect(const float rect[4], float x, float y) {
    return x >= rect[0] && y >= rect[1] && x <= rect[2] && y <= rect[3];
}

/// Puts the touch area back where it starts.
void resetZone(Settings& settings) {
    const Settings defaults;
    settings.anchorX = defaults.anchorX;
    settings.anchorY = defaults.anchorY;
    settings.anchorRadius = defaults.anchorRadius;
}

/// Closes the menu and opens the editor in its place.
void openZoneEditor() {
    g_menuVisible.store(false, std::memory_order_relaxed);
    g_zoneEditing.store(true, std::memory_order_relaxed);
    g_grab = Grab::kNone;
    resetTouch();
}

/// Draws the area, the dot that resizes it, and the small panel with Done and Reset.
void drawZoneEditor(const Settings& settings, const ImVec2& origin) {
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    list->AddRectFilled(ImVec2(0.0f, 0.0f), windowSize, IM_COL32(0, 0, 0, 110));   // dim the game so the area stands out

    const ZoneGeometry zone = zoneGeometry(settings, origin);
    const ImU32 accent = IM_COL32(120, 200, 255, 255);
    list->AddCircleFilled(zone.centre, zone.radius, IM_COL32(120, 200, 255, 46), 96);
    list->AddCircle(zone.centre, zone.radius, accent, 96, dp(1.6f));
    const float arm = dp(7.0f);
    list->AddLine(ImVec2(zone.centre.x - arm, zone.centre.y), ImVec2(zone.centre.x + arm, zone.centre.y), accent, dp(1.0f));
    list->AddLine(ImVec2(zone.centre.x, zone.centre.y - arm), ImVec2(zone.centre.x, zone.centre.y + arm), accent, dp(1.0f));
    list->AddCircleFilled(zone.handle, zoneHandleRadius(), accent);
    list->AddCircle(zone.handle, zoneHandleRadius(), IM_COL32(255, 255, 255, 255), 24, dp(1.6f));

    char size[32];
    std::snprintf(size, sizeof(size), "%.0f px", zone.radius);
    const ImVec2 sizeText = ImGui::CalcTextSize(size);
    list->AddText(ImVec2(zone.centre.x - sizeText.x * 0.5f, zone.centre.y + arm + 6.0f), IM_COL32(255, 255, 255, 255), size);

    ImGui::SetNextWindowPos(ImVec2(windowSize.x * 0.5f, dp(12.0f)), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("##ZoneEditor", nullptr, flags)) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(dp(280.0f), windowSize.x - dp(40.0f)));
        ImGui::TextUnformatted(T(Key::ZoneEditorHint));
        ImGui::PopTextWrapPos();
        if (ImGui::Button(T(Key::Done), ImVec2(dp(84.0f), 0.0f))) {
            g_zoneEditing.store(false, std::memory_order_relaxed);
            g_menuVisible.store(true, std::memory_order_relaxed);
            g_grab = Grab::kNone;
            resetTouch();
        }
        ImGui::SameLine();
        if (ImGui::Button(T(Key::ResetTouchArea))) {
            Settings edited = settings;
            resetZone(edited);
            aimbuddy::settingsStore().set(edited);
        }
        const ImVec2 position = ImGui::GetWindowPos();
        const ImVec2 extent = ImGui::GetWindowSize();
        g_toolbarRect[0] = position.x;
        g_toolbarRect[1] = position.y;
        g_toolbarRect[2] = position.x + extent.x;
        g_toolbarRect[3] = position.y + extent.y;
    }
    ImGui::End();
}

/// Moves the area so that its centre keeps the same distance from the finger.
void moveZoneTo(const ImVec2& finger, const ImVec2& origin) {
    const ImVec2 extent = displayExtent();
    Settings settings = aimbuddy::settingsStore().get();
    settings.anchorX = (finger.x + g_grabOffset.x + origin.x) / extent.x;
    settings.anchorY = (finger.y + g_grabOffset.y + origin.y) / extent.y;
    aimbuddy::settingsStore().set(settings);   // keeps the area inside its allowed range
}

/// Sets the radius to the distance from the area's centre to the finger.
void resizeZoneTo(const ImVec2& finger, const ImVec2& origin) {
    Settings settings = aimbuddy::settingsStore().get();
    const ZoneGeometry zone = zoneGeometry(settings, origin);
    settings.anchorRadius = distanceBetween(finger, zone.centre);
    aimbuddy::settingsStore().set(settings);
}

/// Handles a touch while the editor is open: the panel's buttons go to ImGui, the rest moves or resizes the area.
bool handleZoneTouch(int action, float x, float y) {
    const ImVec2 finger(x, y);
    const ImVec2 origin(static_cast<float>(g_overlayOriginX.load(std::memory_order_relaxed)),
                        static_cast<float>(g_overlayOriginY.load(std::memory_order_relaxed)));
    if (action == 0) {   // finger down: take hold of whatever is under it
        g_grab = Grab::kNone;
        if (insideRect(g_toolbarRect, x, y)) {
            g_grab = Grab::kToolbar;
            queueImGuiDown(x, y);
            return true;
        }
        const ZoneGeometry zone = zoneGeometry(aimbuddy::settingsStore().get(), origin);
        if (distanceBetween(finger, zone.handle) <= zoneHandleGrabRadius()) {
            g_grab = Grab::kResize;
        } else if (distanceBetween(finger, zone.centre) <= zone.radius) {
            g_grab = Grab::kMove;
            g_grabOffset = ImVec2(zone.centre.x - x, zone.centre.y - y);
        }
        return true;
    }
    // The finger keeps what it took hold of until it lifts, wherever it travels.
    if (action == 2) {
        if (g_grab == Grab::kToolbar) {
            queueImGuiMove(x, y);
        } else if (g_grab == Grab::kMove) {
            moveZoneTo(finger, origin);
        } else if (g_grab == Grab::kResize) {
            resizeZoneTo(finger, origin);
        }
        return true;
    }
    if (g_grab == Grab::kToolbar) {
        queueImGuiUp(x, y);
    }
    g_grab = Grab::kNone;
    return true;
}

// ---------------------------------------------------------------------------------------------
// The menu
// ---------------------------------------------------------------------------------------------

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
    changed |= ImGui::Checkbox(T(Key::ShowDetectionArea), &s.showDetectionArea);
    changed |= ImGui::Checkbox(T(Key::ShowAimReach), &s.showAimReach);
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
    if (ImGui::Button(T(Key::MoveTouchArea))) {
        openZoneEditor();
    }
    ImGui::SameLine();
    if (ImGui::Button(T(Key::ResetTouchArea))) {
        resetZone(s);
        changed = true;
    }
    if (ImGui::CollapsingHeader(T(Key::FineTune))) {
        changed |= ImGui::SliderFloat(T(Key::AnchorX), &s.anchorX, 0.05f, 0.95f, "%.2f");
        changed |= ImGui::SliderFloat(T(Key::AnchorY), &s.anchorY, 0.05f, 0.95f, "%.2f");
        changed |= ImGui::SliderFloat(T(Key::AnchorSize), &s.anchorRadius, 50.0f, 500.0f, "%.0f px");
    }
    return changed;
}

/// Resets everything but the language to how it starts, once the button has been pressed twice.
bool drawResetAllButton(Settings& s) {
    static double armedUntil = 0.0;   // while ImGui's clock is before this, the next press confirms
    const bool armed = ImGui::GetTime() < armedUntil;
    if (!ImGui::Button(armed ? T(Key::ResetAllConfirm) : T(Key::ResetAllSettings))) {
        return false;
    }
    if (!armed) {
        armedUntil = ImGui::GetTime() + 4.0;
        return false;
    }
    armedUntil = 0.0;
    s = s.resetKeepingLanguage();
    return true;
}

bool drawInfoTab(Settings& s, int bodyCount) {
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
    ImGui::Spacing();
    return drawResetAllButton(s);
}

/// Draws the menu beside the floating icon. Returns true if a setting changed.
bool drawMenu(Settings& settings, int bodyCount) {
    const ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    if (!g_menuWasVisible || g_menuSize.x <= 0.0f || g_menuSize.y <= 0.0f) {
        g_menuSize = ImVec2(std::max(dp(300.0f), std::min(windowSize.x * 0.46f, dp(440.0f))),
                            std::max(dp(280.0f), std::min(windowSize.y * 0.90f, dp(560.0f))));
    }
    const float iconX = g_iconX.load(std::memory_order_relaxed);
    const float iconY = g_iconY.load(std::memory_order_relaxed);
    float x = iconX + iconHalf() + dp(14.0f);
    float y = iconY - g_menuSize.y * 0.5f;
    if (x + g_menuSize.x > windowSize.x) {
        x = iconX - iconHalf() - dp(14.0f) - g_menuSize.x;
    }
    x = std::max(4.0f, std::min(x, windowSize.x - g_menuSize.x - 4.0f));
    y = std::max(4.0f, std::min(y, windowSize.y - g_menuSize.y - 4.0f));

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(g_menuSize, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(280.0f), dp(240.0f)), ImVec2(windowSize.x - dp(4.0f), windowSize.y - dp(4.0f)));

    bool changed = false;
    bool open = true;
    if (ImGui::Begin(T(Key::AppTitle), &open, ImGuiWindowFlags_NoCollapse)) {
        g_menuSize = ImGui::GetWindowSize();
        const ImVec2 position = ImGui::GetWindowPos();
        g_menuRect[0] = position.x;
        g_menuRect[1] = position.y;
        g_menuRect[2] = position.x + g_menuSize.x;
        g_menuRect[3] = position.y + g_menuSize.y;

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
                changed |= drawInfoTab(settings, bodyCount);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        if (ImGui::Button(T(Key::Close), ImVec2(dp(84.0f), 0.0f))) {
            open = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(T(Key::StopService), ImVec2(0.0f, 0.0f))) {
            NotifyStopRequested();
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

/// Keeps the overlay from drawing faster than needed, and measures how fast it does draw. Returns the seconds since the previous frame.
float limitFrameRate() {
    const int cap = g_menuVisible.load(std::memory_order_relaxed) ? kMenuFpsCap : kBaseFpsCap;
    const auto minFrameTime = std::chrono::nanoseconds(1'000'000'000LL / cap);
    if (g_lastTick.time_since_epoch().count() != 0) {
        const auto elapsed = std::chrono::steady_clock::now() - g_lastTick;
        if (elapsed < minFrameTime) {
            std::this_thread::sleep_for(minFrameTime - elapsed);
        }
    }
    const auto now = std::chrono::steady_clock::now();
    float deltaSeconds = 1.0f / kBaseFpsCap;
    if (g_lastTick.time_since_epoch().count() != 0) {
        const float seconds = std::chrono::duration<float>(now - g_lastTick).count();
        if (seconds > 0.0f && seconds <= 0.25f) {
            deltaSeconds = seconds;
            const float instant = 1.0f / seconds;
            g_measuredFps = g_measuredFps > 0.0f ? g_measuredFps * 0.9f + instant * 0.1f : instant;
        }
    }
    g_lastTick = now;
    return deltaSeconds;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Calls from OverlayNative (see OverlayView.kt)
// ---------------------------------------------------------------------------------------------

/**
 * Starts drawing into the OpenGL context that is current on the calling (GL) thread. Called each
 * time the overlay gets a new surface, and with it a new context. The ImGui context and fonts are
 * made once and kept; the OpenGL backend, which holds objects of one GL context, is set up again.
 */
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_OverlayNative_attach(JNIEnv* env, jclass /* clazz */, jobject assetManager, jint width, jint height,
                                       jfloat density) {
    g_windowWidth = width;
    g_windowHeight = height;
    g_dp = density > 0.0f ? density : 2.0f;
    LOGI("Overlay window: %dx%d", g_windowWidth, g_windowHeight);

    if (!g_contextCreated) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        loadFonts(AAssetManager_fromJava(env, assetManager), dp(kFontDp));
        applyTheme();
        g_contextCreated = true;
    }
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    aimbuddy::i18n::SetLanguage(aimbuddy::settingsStore().get().language);
    g_streamerModeApplied = false;   // a new window starts unsecured; the next frame tells Kotlin if it should not be
    g_lastTick = {};

    if (g_backendReady) {
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_backendReady = true;
    glViewport(0, 0, width, height);
    resetTouch();   // a finger that was on the old window means nothing to the new one
    g_imguiReady.store(true, std::memory_order_release);
    LOGI("ImGui initialised");
}

/// The overlay window changed size (the screen turned, or a bar came or went).
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_OverlayNative_resize(JNIEnv* /* env */, jclass /* clazz */, jint width, jint height) {
    g_windowWidth = width;
    g_windowHeight = height;
    if (!g_imguiReady.load(std::memory_order_acquire)) {
        return;
    }
    glViewport(0, 0, width, height);
    // The overlay is not the screen: it shrinks while a bar is showing. Detection and aiming always
    // work in display pixels (see NativeBridge.configureDisplay).
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
}

/// Where the overlay window's top-left corner is on the display, in pixels.
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_OverlayNative_setOrigin(JNIEnv* /* env */, jclass /* clazz */, jint x, jint y) {
    g_overlayOriginX.store(x, std::memory_order_relaxed);
    g_overlayOriginY.store(y, std::memory_order_relaxed);
}

/// Draws one frame into the current GL context.
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_OverlayNative_drawFrame(JNIEnv* /* env */, jclass /* clazz */) {
    if (!g_imguiReady.load(std::memory_order_acquire)) {
        return;
    }
    try {
        const float deltaSeconds = limitFrameRate();
        g_frameCount.fetch_add(1, std::memory_order_relaxed);

        ImGui_ImplOpenGL3_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(g_windowWidth), static_cast<float>(g_windowHeight));
        io.DeltaTime = deltaSeconds;
        feedTouchToImGui();
        ImGui::NewFrame();

        const Settings settings = aimbuddy::settingsStore().get();
        aimbuddy::i18n::SetLanguage(settings.language);

        const ImVec2 origin(static_cast<float>(g_overlayOriginX.load(std::memory_order_relaxed)),
                            static_cast<float>(g_overlayOriginY.load(std::memory_order_relaxed)));
        int bodyCount = 0;
        // Streamer mode draws nothing over the game, so a recording, and AimBuddy's own capture, see it
        // as it is. The menu and the touch area editor still show what is behind them.
        const bool controlsOpen = g_menuVisible.load(std::memory_order_relaxed) || g_zoneEditing.load(std::memory_order_relaxed);
        if (!settings.streamerMode || controlsOpen) {
            drawGameOverlay(settings, origin, &bodyCount);
        }

        if (g_zoneEditing.load(std::memory_order_relaxed)) {
            drawZoneEditor(settings, origin);
            g_menuWasVisible = false;
        } else if (g_menuVisible.load(std::memory_order_relaxed)) {
            Settings edited = settings;
            if (drawMenu(edited, bodyCount)) {
                aimbuddy::settingsStore().set(edited);
            }
            g_menuWasVisible = true;
        } else {
            g_menuWasVisible = false;
            std::fill(std::begin(g_menuRect), std::end(g_menuRect), 0.0f);
            resetTouch();
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
 *
 * Runs on the GL thread (Kotlin queues it there). The touch is not given to ImGui here but kept as
 * steps that feedTouchToImGui() shows it, spaced by frames.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_OverlayNative_touch(JNIEnv* /* env */, jclass /* clazz */, jint action, jfloat x, jfloat y) {
    if (!g_imguiReady.load(std::memory_order_acquire)) {
        return JNI_FALSE;
    }
    if (g_zoneEditing.load(std::memory_order_relaxed)) {
        return handleZoneTouch(action, x, y) ? JNI_TRUE : JNI_FALSE;
    }
    if (!g_menuVisible.load(std::memory_order_relaxed)) {
        return JNI_FALSE;
    }
    switch (action) {
        case 0:   // finger down: outside the menu it closes it
            if (!insideRect(g_menuRect, x, y)) {
                g_menuVisible.store(false, std::memory_order_relaxed);
                return JNI_TRUE;
            }
            queueImGuiDown(x, y);
            break;
        case 2:   // finger moved
            queueImGuiMove(x, y);
            break;
        default:  // finger up or cancelled
            queueImGuiUp(x, y);
            break;
    }
    return JNI_TRUE;
}

/// Whether the overlay window should be touchable: only while the menu is open.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_OverlayNative_wantsTouches(JNIEnv* /* env */, jclass /* clazz */) {
    const bool wanted = g_menuVisible.load(std::memory_order_relaxed) || g_zoneEditing.load(std::memory_order_relaxed);
    return wanted ? JNI_TRUE : JNI_FALSE;
}

/// Opens the menu, or closes everything that takes touches (the menu and the touch area editor).
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_OverlayNative_showMenu(JNIEnv* /* env */, jclass /* clazz */, jboolean visible) {
    g_menuVisible.store(visible == JNI_TRUE, std::memory_order_relaxed);
    if (visible != JNI_TRUE) {
        g_zoneEditing.store(false, std::memory_order_relaxed);
    }
}

/// Whether the touch area editor is open, so the assist can be paused while the player moves the area.
extern "C" bool IsZoneEditorOpen() { return g_zoneEditing.load(std::memory_order_relaxed); }

/// How many frames the overlay has drawn, so Kotlin can tell a stuck renderer from a working one.
extern "C" JNIEXPORT jlong JNICALL
Java_com_aimbuddy_OverlayNative_framesDrawn(JNIEnv* /* env */, jclass /* clazz */) {
    return static_cast<jlong>(g_frameCount.load(std::memory_order_relaxed));
}

extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_OverlayNative_setIconPosition(JNIEnv* /* env */, jclass /* clazz */, jfloat x, jfloat y) {
    // Kotlin gives the icon's top-left corner; the menu opens beside its centre.
    g_iconX.store(x + iconHalf(), std::memory_order_relaxed);
    g_iconY.store(y + iconHalf(), std::memory_order_relaxed);
}

/**
 * Stops drawing and frees what was made for the GL context, which must still be current on the
 * calling (GL) thread. The ImGui context itself stays for the next surface.
 */
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_OverlayNative_detach(JNIEnv* /* env */, jclass /* clazz */) {
    g_imguiReady.store(false, std::memory_order_release);
    g_menuVisible.store(false, std::memory_order_relaxed);
    g_zoneEditing.store(false, std::memory_order_relaxed);
    resetTouch();
    if (g_backendReady) {
        ImGui_ImplOpenGL3_Shutdown();
        g_backendReady = false;
    }
    LOGI("Overlay detached");
}
