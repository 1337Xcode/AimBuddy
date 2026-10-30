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
#include "core/drag_scroller.h"
#include "core/fingerprint.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "overlay/target_overlay.h"
#include "renderer/menu_widgets.h"
#include "utils/i18n.h"
#include "utils/logger.h"

using aimbuddy::Settings;
using aimbuddy::i18n::Key;
using aimbuddy::i18n::T;
namespace menu = aimbuddy::menu;

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

// How long the draw thread waits before looking again for an ImGui that is not yet attached.
constexpr int kNotReadyWaitMs = 16;

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
std::atomic<bool> g_picking{false};       ///< The player is being asked to tap a colour. The window takes the touches for that.
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

// Where the menu is, or would be if it opened now, in overlay window pixels: left, top, right, bottom. The GL thread
// keeps it current once a frame and Kotlin reads it, to put a secure window over the menu in streamer mode.
std::atomic<int> g_menuBounds[4];

ImVec2 g_menuSize = ImVec2(0.0f, 0.0f);
bool g_menuWasVisible = false;
bool g_streamerModeApplied = false;
std::chrono::steady_clock::time_point g_lastTick{};
float g_measuredFps = 0.0f;

// What the last frame that was drawn looked like. A frame that looks the same is not drawn again, so a game with
// nothing to show over it costs no drawing and no compositing. GL thread only.
uint64_t g_lastDrawnFingerprint = 0;
bool g_hasDrawn = false;

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
    const bool colour = std::strcmp(stats.backend, "Colour") == 0;
    std::snprintf(text, sizeof(text), "%.0f/s  %.0f ms  %s", stats.detectionsPerSecond, stats.frameAgeMs,
                  stats.backend[0] == '\0' ? "" : (colour ? "Colour" : (gpu ? "GPU" : "CPU")));
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float padding = kReadoutPaddingEm * size;
    const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    const ImVec2 corner(windowSize.x - kReadoutMarginEm * size - textSize.x - 2.0f * padding, kReadoutTopEm * size);
    list->AddRectFilled(corner, ImVec2(corner.x + textSize.x + 2.0f * padding, corner.y + textSize.y + 2.0f * padding),
                        IM_COL32(0, 0, 0, 150), 0.3f * size);
    list->AddText(font, size, ImVec2(corner.x + padding, corner.y + padding), IM_COL32(255, 255, 255, 230), text);
}

/// How far past the colour's tolerance (in degrees of hue) the overlay keeps its own colours away from it.
constexpr float kOverlayHueMarginDegrees = 20.0f;

/**
 * What the overlay draws is in the picture the colour detector reads, so while a colour is looked for the overlay
 * must not draw in it: a box of that colour would be found as a target, and a bigger box the next time. A colour
 * near it is turned half way round the colour wheel; greys, and colours far from it, are left as they are.
 */
ImU32 clearOfTarget(ImU32 colour, const Settings& settings) {
    if (!aimbuddy::pipeline().detectsByColour() || !settings.colourLearned) {
        return colour;
    }
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(colour);
    float hue, saturation, value;
    ImGui::ColorConvertRGBtoHSV(rgba.x, rgba.y, rgba.z, hue, saturation, value);
    if (saturation < 0.25f) {
        return colour;
    }
    float apart = std::fabs(hue - settings.colourHue / 360.0f);
    apart = std::min(apart, 1.0f - apart);
    if (apart * 360.0f > settings.colourTolerance + kOverlayHueMarginDegrees) {
        return colour;
    }
    ImGui::ColorConvertHSVtoRGB(std::fmod(hue + 0.5f, 1.0f), saturation, value, rgba.x, rgba.y, rgba.z);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

/// Learning a colour: the player is asked to tap it, the pipeline reads it from the next frame, and the answer is shown.
enum class LearnStage { kIdle, kPicking, kWaiting, kResult };
LearnStage g_learnStage = LearnStage::kIdle;   ///< GL thread only, except that the touch handler ends kPicking (see pickColourAt).
double g_learnStageEnds = 0.0;                 ///< ImGui's clock when the stage is over.
bool g_learnFound = false;
float g_pickCancelRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};   ///< The Cancel button as drawn last: left, top, right, bottom.

constexpr double kLearnWaitS = 3.0;
constexpr double kLearnResultS = 2.0;
constexpr float kPromptScale = 1.1f;   ///< The prompt's text is this much bigger than the menu's, to be read at a glance.

/// Draws what the player is asked to do, or the answer, in a label at the top of the screen, and moves the learning on. The menu comes back after the answer.
void drawColourLearning(ImDrawList* list, const ImVec2& windowSize) {
    if (g_learnStage == LearnStage::kIdle) {
        return;
    }
    const double now = ImGui::GetTime();
    char text[96] = "";
    bool withCancel = false;
    const bool hasAnswer = g_learnStage == LearnStage::kResult;
    switch (g_learnStage) {
        case LearnStage::kPicking:
            if (!g_picking.load(std::memory_order_relaxed)) {
                // The menu was closed from outside (the floating button, the notification), which ends the picking
                // too; the prompt's Cancel could no longer be reached.
                g_learnStage = LearnStage::kIdle;
                return;
            }
            std::snprintf(text, sizeof(text), "%s", T(Key::PickTheColour));
            withCancel = true;
            break;
        case LearnStage::kWaiting: {
            const aimbuddy::DetectionPipeline::ColourLearning answer = aimbuddy::pipeline().colourLearning();
            if (answer != aimbuddy::DetectionPipeline::ColourLearning::kNone || now >= g_learnStageEnds) {
                g_learnFound = answer == aimbuddy::DetectionPipeline::ColourLearning::kLearned;
                g_learnStage = LearnStage::kResult;
                g_learnStageEnds = now + kLearnResultS;
            }
            break;
        }
        case LearnStage::kResult:
            if (now >= g_learnStageEnds) {
                g_learnStage = LearnStage::kIdle;
                g_menuVisible.store(true, std::memory_order_relaxed);   // the menu shows what was learned
                return;
            }
            std::snprintf(text, sizeof(text), "%s", T(g_learnFound ? Key::LearnFound : Key::LearnNotFound));
            break;
        case LearnStage::kIdle:
            return;
    }
    if (text[0] == '\0') {
        return;
    }
    // One rounded bar in the menu's own colours: a dot that says how the learning went, the words, and a Cancel pill
    // while a colour is asked for.
    ImFont* font = ImGui::GetFont();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float size = ImGui::GetFontSize() * kPromptScale;
    const float height = 2.5f * size;
    const float inset = 0.4f * size;               // between the bar's edge and the Cancel pill
    const float edge = 0.9f * size;                // between the bar's left edge and what is in it
    const float gap = 0.6f * size;
    const float dot = hasAnswer ? 0.8f * size : 0.0f;
    const float textWidth = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
    const float cancelWidth = font->CalcTextSizeA(size, FLT_MAX, 0.0f, T(Key::Cancel)).x;
    const float pillWidth = withCancel ? cancelWidth + 1.6f * size : 0.0f;
    const float width = edge + (hasAnswer ? dot + gap : 0.0f) + textWidth + (withCancel ? gap + pillWidth + inset : edge);
    const ImVec2 corner(0.5f * (windowSize.x - width), 2.0f * size);
    const float middle = corner.y + 0.5f * height;

    ImVec4 background = style.Colors[ImGuiCol_WindowBg];
    background.w = 0.96f;
    list->AddRectFilled(corner, ImVec2(corner.x + width, corner.y + height), ImGui::GetColorU32(background), 0.5f * height);
    list->AddRect(corner, ImVec2(corner.x + width, corner.y + height), ImGui::GetColorU32(ImGuiCol_Border), 0.5f * height, 0, 1.0f);

    float cursor = corner.x + edge;
    if (hasAnswer) {
        const ImVec4& colour = g_learnFound ? menu::kSuccessColour : menu::kDangerColour;
        list->AddCircleFilled(ImVec2(cursor + 0.5f * dot, middle), 0.5f * dot, ImGui::GetColorU32(colour));
        cursor += dot + gap;
    }
    list->AddText(font, size, ImVec2(cursor, middle - 0.5f * size), ImGui::GetColorU32(ImGuiCol_Text), text);
    if (withCancel) {
        const float pillLeft = corner.x + width - inset - pillWidth;
        const float pillHeight = height - 2.0f * inset;
        list->AddRectFilled(ImVec2(pillLeft, corner.y + inset), ImVec2(pillLeft + pillWidth, corner.y + inset + pillHeight),
                            ImGui::GetColorU32(ImGuiCol_FrameBgHovered), 0.5f * pillHeight);
        list->AddText(font, size, ImVec2(pillLeft + 0.5f * (pillWidth - cancelWidth), middle - 0.5f * size),
                      ImGui::GetColorU32(ImGuiCol_Text), T(Key::Cancel));
        g_pickCancelRect[0] = pillLeft;
        g_pickCancelRect[1] = corner.y + inset;
        g_pickCancelRect[2] = pillLeft + pillWidth;
        g_pickCancelRect[3] = corner.y + inset + pillHeight;
    }
}

/**
 * Fills `views` with the targets in view, projected to the moment the frame being drawn will be on screen, without the
 * player's own weapon or character: a box stuck to the screen is not a target. Returns how many there are.
 */
int collectTargets(aimbuddy::TrackView* views) {
    const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now().time_since_epoch()).count();
    const int viewCount = aimbuddy::pipeline().tracker().snapshot(nowNs + kDisplayLeadNs, views, aimbuddy::kMaxTracks);
    int kept = 0;
    for (int i = 0; i < viewCount; ++i) {
        if (!aimbuddy::aimLoop().isDecoy(views[i], nowNs)) {
            views[kept++] = views[i];
        }
    }
    return kept;
}

/// How many of the targets are people, not what the model also marks (a weapon, say).
int countBodies(const aimbuddy::TrackView* views, int count) {
    const aimbuddy::ModelProfile& profile = aimbuddy::pipeline().profile();
    int bodies = 0;
    for (int i = 0; i < count; ++i) {
        if (profile.roleOf(views[i].classId) == aimbuddy::ClassRole::kBody) {
            ++bodies;
        }
    }
    return bodies;
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
    const bool showGuides = g_menuVisible.load(std::memory_order_relaxed) || g_zoneEditing.load(std::memory_order_relaxed) ||
                            g_picking.load(std::memory_order_relaxed);
    if (showGuides) {
        const float arm = 11.5f;
        const ImU32 crossColor = clearOfTarget(IM_COL32(58, 156, 255, 240), settings);
        list->AddLine(ImVec2(crosshair.x - arm, crosshair.y), ImVec2(crosshair.x + arm, crosshair.y), crossColor, 3.0f);
        list->AddLine(ImVec2(crosshair.x, crosshair.y - arm), ImVec2(crosshair.x, crosshair.y + arm), crossColor, 3.0f);
    }

    aimbuddy::TrackView views[aimbuddy::kMaxTracks];
    const int viewCount = collectTargets(views);
    const aimbuddy::ModelProfile& profile = aimbuddy::pipeline().profile();

    aimbuddy::TargetStyle style;
    style.color = clearOfTarget(
        ImGui::ColorConvertFloat4ToU32(ImVec4(settings.boxColor[0], settings.boxColor[1], settings.boxColor[2], 1.0f)), settings);
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
        // From the crosshair to the target's middle; from an edge of the screen to the side of the box facing it.
        const float centerX = nearest->centerX - origin.x;
        const float halfHeight = nearest->height * 0.5f;
        ImVec2 from = crosshair;
        ImVec2 to(centerX, nearest->centerY - origin.y);
        if (settings.snapLineFrom == 1) {
            from = ImVec2(displayWidth * 0.5f - origin.x, -origin.y);
            to.y -= halfHeight;
        } else if (settings.snapLineFrom == 2) {
            from = ImVec2(displayWidth * 0.5f - origin.x, displayHeight - origin.y);
            to.y += halfHeight;
        }
        list->AddLine(from, to, clearOfTarget(IM_COL32(255, 100, 50, static_cast<int>(255.0f * nearest->opacity)), settings),
                      2.0f);
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
        list->AddCircle(crosshair, settings.aimFovRadius,
                        clearOfTarget(settings.assistEnabled ? kAimFovColor : kAimFovIdleColor, settings), 64, kFovLineWidth);
    }

    // Where the assist's finger rests, so the player can keep their own thumb away from it. The
    // editor draws the area itself while it is open.
    if (settings.assistEnabled && settings.showAnchor && !g_zoneEditing.load(std::memory_order_relaxed)) {
        const aimbuddy::TouchArea area = settings.touchAreaOn(displayWidth, displayHeight);
        const ImVec2 anchor(area.centerX - origin.x, area.centerY - origin.y);
        const int alpha = static_cast<int>(255.0f * settings.anchorOpacity);
        list->AddCircleFilled(anchor, area.radiusPx, IM_COL32(120, 200, 255, alpha / 4), 64);
        list->AddCircle(anchor, area.radiusPx, IM_COL32(120, 200, 255, alpha), 64, 2.0f);
    }

    if (settings.showCount && bodies > 0) {
        char text[64];
        std::snprintf(text, sizeof(text), "%d", bodies);
        ImFont* font = ImGui::GetFont();
        const float size = ImGui::GetFontSize() * 2.0f;
        const ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
        list->AddText(font, size, ImVec2((windowSize.x - textSize.x) * 0.5f, 40.0f), clearOfTarget(IM_COL32(255, 50, 50, 255), settings), text);
    }

    if (settings.showPerformance) {
        drawPerformanceReadout(list, windowSize);
    }
    drawColourLearning(list, windowSize);
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

/// Kotlin sends 0 for a finger down, 1 up, 2 moved, and this when the system takes the gesture back.
constexpr int kTouchCancelled = 3;

std::deque<TouchStep> g_touchSteps;   ///< What the finger did, not yet shown to ImGui. GL thread only.
bool g_pressShown = false;            ///< ImGui has been told the button is down.
int g_framesPressed = 0;              ///< Frames the button has been down.

// A finger that goes down on a page of the menu may scroll it instead of pressing a control (see DragScroller).
constexpr float kScrollSlopDp = 8.0f;
aimbuddy::DragScroller g_scroller(0.0f);   ///< Given its real slop when the overlay attaches. GL thread only.
float g_pendingScrollPx = 0.0f;            ///< How far the finger has scrolled the page since it was last moved. GL thread only.

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

/// Gives the menu what a finger did (`action` as in OverlayNative.touch), as the scroller decided: to the controls, or to the page.
void deliverToMenu(const aimbuddy::ScrollDecision& decision, int action, float x, float y) {
    using Kind = aimbuddy::ScrollDecision::Kind;
    switch (decision.kind) {
        case Kind::kHold:
            return;
        case Kind::kScroll:
            g_pendingScrollPx += decision.scrollPx;
            return;
        case Kind::kPress:
            queueImGuiDown(decision.pressX, decision.pressY);   // the controls get the press from where the finger went down
            break;
        case Kind::kPass:
            if (action == 0) {
                queueImGuiDown(x, y);
                return;
            }
            break;
    }
    if (action == 2) {
        queueImGuiMove(x, y);
    } else {
        queueImGuiUp(x, y);
    }
}

/// Forgets the finger, so a press that was cut short (the menu closed) cannot stay down for next time.
void resetTouch() {
    g_scroller.cancel();
    g_pendingScrollPx = 0.0f;
    if (!g_pressShown && g_touchSteps.empty()) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    g_touchSteps.clear();
    // The pointer leaves the controls before the press is let go, so that letting go clicks nothing.
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    if (g_pressShown) {
        io.AddMouseButtonEvent(0, false);
        g_pressShown = false;
    }
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
    const aimbuddy::TouchArea area = settings.touchAreaOn(extent.x, extent.y);   // what the assist really uses
    ZoneGeometry zone;
    zone.centre = ImVec2(area.centerX - origin.x, area.centerY - origin.y);
    zone.radius = area.radiusPx;
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

/// Closes the menu and asks the player to tap the colour to look for, inside the area the detector reads.
void startColourPicking() {
    g_menuVisible.store(false, std::memory_order_relaxed);
    g_grab = Grab::kNone;
    resetTouch();
    g_learnStage = LearnStage::kPicking;
    g_picking.store(true, std::memory_order_relaxed);
}

/// Handles a touch while the player is asked to tap a colour: Cancel gives up, a tap inside the detector's area picks the colour there.
bool handlePickTouch(int action, float x, float y, const ImVec2& origin) {
    if (action == 0) {
        if (insideRect(g_pickCancelRect, x, y)) {
            g_picking.store(false, std::memory_order_relaxed);
            g_learnStage = LearnStage::kIdle;
            g_menuVisible.store(true, std::memory_order_relaxed);
        }
        return true;
    }
    if (action != 1) {
        return true;   // a finger moving or lost is not a tap
    }
    float left, top, right, bottom;
    const float screenX = x + origin.x;
    const float screenY = y + origin.y;
    if (insideRect(g_pickCancelRect, x, y) || !GetDetectionZoneScreenRect(&left, &top, &right, &bottom) || screenX < left ||
        screenX > right || screenY < top || screenY > bottom) {
        return true;   // outside the area the detector reads: the prompt stays
    }
    aimbuddy::pipeline().requestColourPick(screenX, screenY);
    g_picking.store(false, std::memory_order_relaxed);
    g_learnStage = LearnStage::kWaiting;
    g_learnStageEnds = ImGui::GetTime() + kLearnWaitS;
    return true;
}

/// Closes the tools that draw over the game, which streamer mode does not offer: the touch area editor, and picking a colour.
void abandonSetupTools() {
    if (g_zoneEditing.load(std::memory_order_relaxed) || g_picking.load(std::memory_order_relaxed)) {
        g_zoneEditing.store(false, std::memory_order_relaxed);
        g_picking.store(false, std::memory_order_relaxed);
        g_learnStage = LearnStage::kIdle;
        g_grab = Grab::kNone;
        resetTouch();
    }
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
            aimbuddy::settingsStore().applyEdit(settings, edited);
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
    const float anchorX = (finger.x + g_grabOffset.x + origin.x) / extent.x;
    const float anchorY = (finger.y + g_grabOffset.y + origin.y) / extent.y;
    aimbuddy::settingsStore().update([anchorX, anchorY](Settings& settings) {   // the store keeps the area inside its allowed range
        settings.anchorX = anchorX;
        settings.anchorY = anchorY;
    });
}

/// Sets the radius to the distance from the area's centre to the finger.
void resizeZoneTo(const ImVec2& finger, const ImVec2& origin) {
    const ImVec2 extent = displayExtent();
    aimbuddy::settingsStore().update([&](Settings& settings) {
        const ZoneGeometry zone = zoneGeometry(settings, origin);
        settings.anchorRadius = distanceBetween(finger, zone.centre);
        settings.anchorRadius = settings.touchAreaOn(extent.x, extent.y).radiusPx;   // the area stops at the edge of the screen
    });
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
    if (action == kTouchCancelled) {
        if (g_grab == Grab::kToolbar) {
            resetTouch();
        }
        g_grab = Grab::kNone;
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

/// How the colour looked for is shown, in font heights: the round dot with the colour in it, and the button that learns another.
constexpr float kColourDotEm = 2.4f;
constexpr float kLearnButtonEm = 2.1f;
constexpr float kColourGapEm = 0.7f;      ///< Between the dot and the words beside it.
constexpr float kLearnPaddingEm = 0.9f;   ///< Between the learn button's ends and its label.

/// Draws the colour looked for as a round dot, or as an empty ring while none is learned. `topLeft` is the corner of the dot's square.
void drawColourDot(const ImVec2& topLeft, float diameter, const Settings& s) {
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 centre(topLeft.x + 0.5f * diameter, topLeft.y + 0.5f * diameter);
    if (!s.colourLearned) {
        list->AddCircle(centre, 0.5f * diameter - 1.0f, ImGui::GetColorU32(ImGuiCol_TextDisabled), 0, 2.0f);
        return;
    }
    float red, green, blue;
    ImGui::ColorConvertHSVtoRGB(s.colourHue / 360.0f, std::min(1.0f, s.colourSaturation + 0.3f), 1.0f, red, green, blue);
    list->AddCircleFilled(centre, 0.5f * diameter, ImGui::GetColorU32(ImVec4(red, green, blue, 1.0f)));
    list->AddCircle(centre, 0.5f * diameter, IM_COL32(255, 255, 255, 70), 0, 1.5f);   // keeps a dark colour in view on the dark card
}

/**
 * Draws the dot, what is known of the colour, and the button that learns it. They share a line when the card is wide
 * enough; when it is not, the button gets a line of its own across the card. Returns true when the button was tapped.
 */
bool drawColourRow(const Settings& s) {
    const float font = ImGui::GetFontSize();
    const float dot = kColourDotEm * font;
    const float gap = kColourGapEm * font;
    const float rowWidth = menu::ContentWidth();
    const char* status = s.colourLearned ? T(Key::ColourLearned) : T(Key::ColourNotLearned);
    const char* learn = T(Key::LearnColour);
    const float buttonHeight = kLearnButtonEm * font;
    const float buttonWidth = ImGui::CalcTextSize(learn).x + 2.0f * kLearnPaddingEm * font;
    const bool sideBySide = dot + gap + ImGui::CalcTextSize(status).x + gap + buttonWidth <= rowWidth;
    const menu::PillStyle style = s.colourLearned ? menu::PillStyle::kTinted : menu::PillStyle::kPrimary;   // with nothing learned, learning is the next step

    const ImVec2 row = ImGui::GetCursorScreenPos();
    drawColourDot(row, dot, s);
    ImGui::SetCursorScreenPos(ImVec2(row.x + dot + gap, row.y + 0.5f * (dot - ImGui::GetTextLineHeight())));
    ImGui::TextUnformatted(status);
    ImGui::BeginDisabled(s.streamerMode);   // picking a colour draws over the game
    bool tapped = false;
    if (sideBySide) {
        ImGui::SetCursorScreenPos(ImVec2(row.x + rowWidth - buttonWidth, row.y + 0.5f * (dot - buttonHeight)));
        tapped = menu::PillButton(learn, ImVec2(buttonWidth, buttonHeight), style);
        ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + dot));   // the next control starts under the dot
        ImGui::Dummy(ImVec2(rowWidth, 0.0f));
    } else {
        ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + dot + ImGui::GetStyle().ItemSpacing.y));
        tapped = menu::PillButton(learn, ImVec2(rowWidth, buttonHeight), style);
    }
    ImGui::EndDisabled();
    return tapped;
}

/// What is looked for when targets are found by colour: the colour and the button that learns it, how strictly it is matched, and how to learn it.
bool drawColourSection(Settings& s) {
    bool changed = false;
    ImGui::SeparatorText(T(Key::DetectColour));
    menu::BeginCard();
    if (drawColourRow(s)) {
        startColourPicking();
    }
    menu::Divider();
    changed |= menu::Slider(T(Key::ColourTolerance), &s.colourTolerance, 4.0f, 40.0f, "%.0f deg");
    changed |= menu::Slider(T(Key::ColourMinHeight), &s.colourMinHeight, 10.0f, 300.0f, "%.0f px");
    menu::EndCard();
    menu::Footnote(T(s.streamerMode ? Key::NotInStreamerMode : Key::LearnColourHint));
    return changed;
}

bool drawVisualsTab(Settings& s) {
    bool changed = false;
    changed |= ImGui::Checkbox(T(Key::ShowLabels), &s.showLabels);
    changed |= ImGui::Checkbox(T(Key::ShowSnapLine), &s.showSnapLine);
    if (s.showSnapLine) {
        const char* lineStarts[] = {T(Key::SnapLineCrosshair), T(Key::SnapLineTop), T(Key::SnapLineBottom)};
        changed |= menu::Choice(T(Key::SnapLineFrom), &s.snapLineFrom, lineStarts, 3);
    }
    changed |= ImGui::Checkbox(T(Key::ShowAimPoint), &s.showAimPoint);
    changed |= ImGui::Checkbox(T(Key::ShowDetectionArea), &s.showDetectionArea);
    changed |= ImGui::Checkbox(T(Key::ShowAimReach), &s.showAimReach);
    changed |= ImGui::Checkbox(T(Key::ShowCount), &s.showCount);
    changed |= ImGui::Checkbox(T(Key::ShowPerformance), &s.showPerformance);
    ImGui::Separator();

    changed |= ImGui::ColorEdit3(T(Key::BoxColor), s.boxColor, ImGuiColorEditFlags_NoInputs);
    changed |= menu::SliderInt(T(Key::BoxThickness), &s.boxThickness, 1, 6, "%d");
    ImGui::Separator();

    if (aimbuddy::pipeline().detectsByColour()) {
        changed |= drawColourSection(s);
    } else {
        changed |= menu::Slider(T(Key::Confidence), &s.confidence, 0.1f, 0.95f, "%.2f");
    }
    changed |= menu::Slider(T(Key::DetectionZone), &s.detectionZoneRadius, 100.0f, 650.0f, "%.0f px");
    ImGui::Separator();

    changed |= ImGui::Checkbox(T(Key::ShowAnchor), &s.showAnchor);
    if (s.showAnchor) {
        changed |= menu::Slider(T(Key::AnchorOpacity), &s.anchorOpacity, 0.1f, 1.0f, "%.2f");
    }
    ImGui::Separator();

    changed |= ImGui::Checkbox(T(Key::StreamerMode), &s.streamerMode);
    menu::Footnote(T(Key::StreamerModeHint));
    const char* languages[] = {aimbuddy::i18n::LanguageName(0), aimbuddy::i18n::LanguageName(1),
                               aimbuddy::i18n::LanguageName(2)};
    if (menu::Choice(T(Key::Language), &s.language, languages, 3)) {
        aimbuddy::i18n::SetLanguage(s.language);
        changed = true;
    }
    return changed;
}

/// A button that stands alone in a page is this tall, in font heights, and as wide as the page.
constexpr float kPageButtonEm = 2.2f;

bool drawAimTab(Settings& s) {
    bool changed = false;
    changed |= ImGui::Checkbox(T(Key::AssistEnable), &s.assistEnabled);

    const aimbuddy::AimLoop::Stats stats = aimbuddy::aimLoop().stats();
    ImGui::TextWrapped("%s", helperStatusText(stats));
    ImGui::Separator();

    const char* zones[] = {T(Key::ZoneHead), T(Key::ZoneChest), T(Key::ZoneCentre)};
    changed |= menu::Choice(T(Key::AimZone), &s.aimZone, zones, 3);
    changed |= menu::Slider(T(Key::AimStrength), &s.aimStrength, 0.0f, 1.0f, "%.2f");
    changed |= menu::Slider(T(Key::AimRadius), &s.aimFovRadius, 50.0f, 650.0f, "%.0f px");
    const char* priorities[] = {T(Key::PriorityNearest), T(Key::PriorityLargest), T(Key::PriorityCertain)};
    changed |= menu::Choice(T(Key::AimPriority), &s.aimPriority, priorities, 3);
    const char* afterLanding[] = {T(Key::AfterKeepFollowing), T(Key::AfterOneCorrection)};
    changed |= menu::Choice(T(Key::AimAfterLanding), &s.aimAfterLanding, afterLanding, 2);
    const char* switching[] = {T(Key::SwitchSticky), T(Key::SwitchBalanced), T(Key::SwitchQuick)};
    changed |= menu::Choice(T(Key::AimSwitching), &s.aimSwitching, switching, 3);

    ImGui::SeparatorText(T(Key::CalibrationTitle));
    ImGui::TextWrapped("%s", T(Key::CalibrationHint));
    changed |= menu::Slider(T(Key::CameraSensitivity), &s.sceneGain, 0.2f, 6.0f, "%.2f");
    changed |= menu::Slider(T(Key::ResponseDelay), &s.actuationDelayMs, 0.0f, 200.0f, "%.0f ms");
    changed |= ImGui::Checkbox(T(Key::InvertVertical), &s.invertY);

    ImGui::SeparatorText(T(Key::TouchAreaTitle));
    ImGui::TextWrapped("%s", T(Key::TouchAreaHint));
    const ImVec2 buttonSize(menu::ContentWidth(), kPageButtonEm * ImGui::GetFontSize());
    ImGui::BeginDisabled(s.streamerMode);   // the editor draws over the game
    if (menu::PillButton(T(Key::MoveTouchArea), buttonSize, menu::PillStyle::kTinted)) {
        openZoneEditor();
    }
    ImGui::EndDisabled();
    if (menu::PillButton(T(Key::ResetTouchArea), buttonSize, menu::PillStyle::kTinted)) {
        resetZone(s);
        changed = true;
    }
    if (s.streamerMode) {
        menu::Footnote(T(Key::NotInStreamerMode));
    }
    if (ImGui::CollapsingHeader(T(Key::FineTune))) {
        changed |= menu::Slider(T(Key::AnchorX), &s.anchorX, 0.05f, 0.95f, "%.2f");
        changed |= menu::Slider(T(Key::AnchorY), &s.anchorY, 0.05f, 0.95f, "%.2f");
        changed |= menu::Slider(T(Key::AnchorSize), &s.anchorRadius, 50.0f, 500.0f, "%.0f px");
    }
    return changed;
}

/// Resets everything but the language to how it starts, once the button has been pressed twice.
bool drawResetAllButton(Settings& s) {
    static double armedUntil = 0.0;   // while ImGui's clock is before this, the next press confirms
    const bool armed = ImGui::GetTime() < armedUntil;
    const ImVec2 size(menu::ContentWidth(), kPageButtonEm * ImGui::GetFontSize());
    const menu::PillStyle style = armed ? menu::PillStyle::kDanger : menu::PillStyle::kTinted;   // it turns red when it asks again
    if (!menu::PillButton(armed ? T(Key::ResetAllConfirm) : T(Key::ResetAllSettings), size, style)) {
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

/// The footer's buttons are this tall, in font heights: tall enough for a thumb.
constexpr float kFooterButtonEm = 2.4f;

/// The room, in font heights, between the page and the footer's buttons. The divider is in it.
constexpr float kFooterGapEm = 0.4f;

/// The height the footer takes under the page: the gap with its divider, and the buttons.
float footerHeight() {
    return ImGui::GetStyle().ItemSpacing.y + (kFooterGapEm + kFooterButtonEm) * ImGui::GetFontSize();
}

/// Draws the divider and, under it, Close and Stop as two buttons of one width. Close sets `open` to false.
void drawFooter(bool* open) {
    const float font = ImGui::GetFontSize();
    const ImGuiStyle& style = ImGui::GetStyle();

    // A line across the whole window says that what is under it stays put while the page scrolls.
    const ImVec2 windowPosition = ImGui::GetWindowPos();
    const float dividerY = ImGui::GetCursorScreenPos().y - 0.5f * style.ItemSpacing.y;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(windowPosition.x, dividerY),
                                        ImVec2(windowPosition.x + ImGui::GetWindowWidth(), dividerY),
                                        ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kFooterGapEm * font);

    const ImVec2 size(0.5f * (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x), kFooterButtonEm * font);
    if (menu::PillButton(T(Key::Close), size, menu::PillStyle::kPrimary)) {
        *open = false;
    }
    ImGui::SameLine();
    if (menu::PillButton(T(Key::StopService), size, menu::PillStyle::kDanger)) {
        NotifyStopRequested();
    }
}

/**
 * Runs `drawPage` in a scrolling area of its own that fills the room between the tabs and the footer. Each tab has
 * one, so that it keeps its place when the player switches away and back, and the tabs themselves never scroll off.
 * Returns what `drawPage` returns: true if a setting changed.
 */
template <typename DrawPage>
bool drawScrollingPage(const char* id, DrawPage&& drawPage) {
    ImGui::BeginChild(id, ImVec2(0, -footerHeight()), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // A finger that goes down on the page may scroll it. Not on the scrollbar, which takes the finger itself (its grab
    // is wider than it looks, so the strip is too), and not while a list is open, which takes every touch.
    const ImVec2 position = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float scrollbarStrip = ImGui::GetStyle().ScrollbarSize + ImGui::GetFontSize();
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
        g_scroller.setArea(0.0f, 0.0f, 0.0f, 0.0f);
    } else {
        g_scroller.setArea(position.x, position.y, position.x + size.x - scrollbarStrip, position.y + size.y);
    }

    const bool changed = drawPage();
    if (g_pendingScrollPx != 0.0f) {
        ImGui::SetScrollY(ImGui::GetScrollY() + g_pendingScrollPx);
        g_pendingScrollPx = 0.0f;
    }
    ImGui::EndChild();
    return changed;
}

/// How big the menu opens: most of the window's height, and a share of its width that is never narrower than a phone's short side allows.
ImVec2 defaultMenuSize(const ImVec2& windowSize) {
    return ImVec2(std::max(dp(300.0f), std::min(windowSize.x * 0.46f, dp(440.0f))),
                  std::max(dp(280.0f), std::min(windowSize.y * 0.90f, dp(560.0f))));
}

/// Where a menu of this size goes: beside the floating icon, on the side that has the room, and inside the window.
ImVec2 menuPositionFor(const ImVec2& size, const ImVec2& windowSize) {
    const float iconX = g_iconX.load(std::memory_order_relaxed);
    const float iconY = g_iconY.load(std::memory_order_relaxed);
    float x = iconX + iconHalf() + dp(14.0f);
    float y = iconY - size.y * 0.5f;
    if (x + size.x > windowSize.x) {
        x = iconX - iconHalf() - dp(14.0f) - size.x;
    }
    x = std::max(4.0f, std::min(x, windowSize.x - size.x - 4.0f));
    y = std::max(4.0f, std::min(y, windowSize.y - size.y - 4.0f));
    return ImVec2(x, y);
}

/// Tells Kotlin where the menu is, or where it would open. Called once a frame from the GL thread.
void publishMenuBounds(const ImVec2& windowSize) {
    float rect[4];
    if (g_menuVisible.load(std::memory_order_relaxed) && g_menuRect[2] > g_menuRect[0]) {
        std::copy(std::begin(g_menuRect), std::end(g_menuRect), rect);
    } else {
        const ImVec2 size = defaultMenuSize(windowSize);
        const ImVec2 position = menuPositionFor(size, windowSize);
        rect[0] = position.x;
        rect[1] = position.y;
        rect[2] = position.x + size.x;
        rect[3] = position.y + size.y;
    }
    // Rounded outwards, so that the pixels the menu covers are all inside what is reported.
    g_menuBounds[0].store(static_cast<int>(std::floor(rect[0])), std::memory_order_relaxed);
    g_menuBounds[1].store(static_cast<int>(std::floor(rect[1])), std::memory_order_relaxed);
    g_menuBounds[2].store(static_cast<int>(std::ceil(rect[2])), std::memory_order_relaxed);
    g_menuBounds[3].store(static_cast<int>(std::ceil(rect[3])), std::memory_order_relaxed);
}

/// Draws the menu beside the floating icon. Returns true if a setting changed.
bool drawMenu(Settings& settings, int bodyCount) {
    const ImVec2 windowSize = ImGui::GetIO().DisplaySize;
    if (!g_menuWasVisible || g_menuSize.x <= 0.0f || g_menuSize.y <= 0.0f) {
        g_menuSize = defaultMenuSize(windowSize);
    }

    ImGui::SetNextWindowPos(menuPositionFor(g_menuSize, windowSize), ImGuiCond_Always);
    ImGui::SetNextWindowSize(g_menuSize, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(280.0f), dp(240.0f)), ImVec2(windowSize.x - dp(4.0f), windowSize.y - dp(4.0f)));

    bool changed = false;
    bool open = true;
    // In streamer mode the menu keeps the size it has, so that the secure window Kotlin puts over it always matches.
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | (settings.streamerMode ? ImGuiWindowFlags_NoResize : 0);
    if (ImGui::Begin(T(Key::AppTitle), &open, flags)) {
        g_menuSize = ImGui::GetWindowSize();
        const ImVec2 position = ImGui::GetWindowPos();
        g_menuRect[0] = position.x;
        g_menuRect[1] = position.y;
        g_menuRect[2] = position.x + g_menuSize.x;
        g_menuRect[3] = position.y + g_menuSize.y;

        if (ImGui::BeginTabBar("##MenuTabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
            if (ImGui::BeginTabItem(T(Key::TabVisuals))) {
                changed |= drawScrollingPage("##VisualsPage", [&] { return drawVisualsTab(settings); });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(T(Key::TabAim))) {
                changed |= drawScrollingPage("##AimPage", [&] { return drawAimTab(settings); });
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(T(Key::TabInfo))) {
                changed |= drawScrollingPage("##InfoPage", [&] { return drawInfoTab(settings, bodyCount); });
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        drawFooter(&open);
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

/// A fingerprint of what a frame would draw: where, with which texture, and every vertex and index it uses.
uint64_t fingerprintOf(const ImDrawData* frame) {
    uint64_t fingerprint = aimbuddy::mixIntoFingerprint(aimbuddy::kFingerprintStart, &frame->DisplayPos, sizeof(frame->DisplayPos));
    fingerprint = aimbuddy::mixIntoFingerprint(fingerprint, &frame->DisplaySize, sizeof(frame->DisplaySize));
    fingerprint = aimbuddy::mixIntoFingerprint(fingerprint, &frame->FramebufferScale, sizeof(frame->FramebufferScale));
    for (const ImDrawList* list : frame->CmdLists) {
        fingerprint = aimbuddy::mixIntoFingerprint(fingerprint, list->CmdBuffer.Data, static_cast<size_t>(list->CmdBuffer.Size) * sizeof(ImDrawCmd));
        fingerprint = aimbuddy::mixIntoFingerprint(fingerprint, list->VtxBuffer.Data, static_cast<size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
        fingerprint = aimbuddy::mixIntoFingerprint(fingerprint, list->IdxBuffer.Data, static_cast<size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
    }
    return fingerprint;
}

/// Whether the backend has a texture to make, change or drop: that happens when the next frame is drawn, whatever it looks like.
bool hasTextureWork(const ImDrawData* frame) {
    if (frame->Textures == nullptr) {
        return false;
    }
    for (const ImTextureData* texture : *frame->Textures) {
        if (texture->Status != ImTextureStatus_OK) {
            return true;
        }
    }
    return false;
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
    g_scroller = aimbuddy::DragScroller(dp(kScrollSlopDp));
    aimbuddy::i18n::SetLanguage(aimbuddy::settingsStore().get().language);
    g_streamerModeApplied = false;   // a new window starts unsecured; the next frame tells Kotlin if it should not be
    g_lastTick = {};
    g_hasDrawn = false;              // a new surface shows nothing yet

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
    g_hasDrawn = false;   // the surface changed size: what it showed is gone
}

/// Where the overlay window's top-left corner is on the display, in pixels.
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_OverlayNative_setOrigin(JNIEnv* /* env */, jclass /* clazz */, jint x, jint y) {
    g_overlayOriginX.store(x, std::memory_order_relaxed);
    g_overlayOriginY.store(y, std::memory_order_relaxed);
}

/**
 * Draws one frame into the current GL context, and says whether it did. A frame that looks like the last one
 * drawn is not drawn again (false): the view already shows it, and the caller must not swap buffers for it.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_OverlayNative_drawFrame(JNIEnv* /* env */, jclass /* clazz */) {
    if (!g_imguiReady.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kNotReadyWaitMs));
        return JNI_FALSE;
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
        if (settings.streamerMode) {
            // Streamer mode draws nothing over the game, so a recording, and AimBuddy's own capture, see it as it
            // is. The menu is all that shows, and Kotlin hides it from captures with a secure window of its size.
            abandonSetupTools();
            aimbuddy::TrackView views[aimbuddy::kMaxTracks];
            bodyCount = countBodies(views, collectTargets(views));
        } else {
            drawGameOverlay(settings, origin, &bodyCount);
        }

        if (g_zoneEditing.load(std::memory_order_relaxed)) {
            drawZoneEditor(settings, origin);
            g_menuWasVisible = false;
        } else if (g_menuVisible.load(std::memory_order_relaxed)) {
            Settings edited = settings;
            if (drawMenu(edited, bodyCount)) {
                aimbuddy::settingsStore().applyEdit(settings, edited);
            }
            g_menuWasVisible = true;
        } else {
            g_menuWasVisible = false;
            std::fill(std::begin(g_menuRect), std::end(g_menuRect), 0.0f);
            resetTouch();
        }
        saveSettingsWhenDue();
        publishMenuBounds(ImGui::GetIO().DisplaySize);

        if (settings.streamerMode != g_streamerModeApplied) {
            g_streamerModeApplied = settings.streamerMode;
            NotifyStreamerModeChanged(g_streamerModeApplied);
        }

        ImGui::Render();
        const ImDrawData* frame = ImGui::GetDrawData();
        const uint64_t fingerprint = fingerprintOf(frame);
        if (g_hasDrawn && fingerprint == g_lastDrawnFingerprint && !hasTextureWork(frame)) {
            return JNI_FALSE;
        }
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        g_lastDrawnFingerprint = fingerprint;
        g_hasDrawn = true;
        return JNI_TRUE;
    } catch (const std::exception& e) {
        LOGE("Exception while drawing the overlay: %s", e.what());
    } catch (...) {
        LOGE("Unknown exception while drawing the overlay");
    }
    g_hasDrawn = false;   // what is on the surface is not known: the next frame is drawn whatever it looks like
    return JNI_FALSE;
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
    if (g_picking.load(std::memory_order_relaxed)) {
        const ImVec2 origin(static_cast<float>(g_overlayOriginX.load(std::memory_order_relaxed)),
                            static_cast<float>(g_overlayOriginY.load(std::memory_order_relaxed)));
        return handlePickTouch(action, x, y, origin) ? JNI_TRUE : JNI_FALSE;
    }
    if (g_zoneEditing.load(std::memory_order_relaxed)) {
        return handleZoneTouch(action, x, y) ? JNI_TRUE : JNI_FALSE;
    }
    if (!g_menuVisible.load(std::memory_order_relaxed)) {
        return JNI_FALSE;
    }
    switch (action) {
        case 0:   // finger down: outside the menu it closes it, unless a list is open, which may reach out past the menu's edge
            if (!insideRect(g_menuRect, x, y) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
                g_menuVisible.store(false, std::memory_order_relaxed);
                return JNI_TRUE;
            }
            deliverToMenu(g_scroller.down(x, y), action, x, y);
            break;
        case 2:   // finger moved
            deliverToMenu(g_scroller.move(x, y), action, x, y);
            break;
        case kTouchCancelled:   // the system took the touch back: whatever it pressed is let go without a click
            resetTouch();
            break;
        default:  // finger up
            deliverToMenu(g_scroller.up(x, y), action, x, y);
            break;
    }
    return JNI_TRUE;
}

/// Whether the overlay window should be touchable: only while the menu is open.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_aimbuddy_OverlayNative_wantsTouches(JNIEnv* /* env */, jclass /* clazz */) {
    const bool wanted = g_menuVisible.load(std::memory_order_relaxed) || g_zoneEditing.load(std::memory_order_relaxed) ||
                        g_picking.load(std::memory_order_relaxed);
    return wanted ? JNI_TRUE : JNI_FALSE;
}

/// Where the menu is, or would open if it opened now, in overlay window pixels: left, top, right, bottom.
extern "C" JNIEXPORT jintArray JNICALL
Java_com_aimbuddy_OverlayNative_menuBounds(JNIEnv* env, jclass /* clazz */) {
    const jint bounds[4] = {g_menuBounds[0].load(std::memory_order_relaxed), g_menuBounds[1].load(std::memory_order_relaxed),
                            g_menuBounds[2].load(std::memory_order_relaxed), g_menuBounds[3].load(std::memory_order_relaxed)};
    jintArray result = env->NewIntArray(4);
    if (result != nullptr) {
        env->SetIntArrayRegion(result, 0, 4, bounds);
    }
    return result;
}

/// Opens the menu, or closes everything that takes touches (the menu and the touch area editor).
extern "C" JNIEXPORT void JNICALL
Java_com_aimbuddy_OverlayNative_showMenu(JNIEnv* /* env */, jclass /* clazz */, jboolean visible) {
    g_menuVisible.store(visible == JNI_TRUE, std::memory_order_relaxed);
    if (visible != JNI_TRUE) {
        g_zoneEditing.store(false, std::memory_order_relaxed);
        g_picking.store(false, std::memory_order_relaxed);
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
    g_picking.store(false, std::memory_order_relaxed);
    g_learnStage = LearnStage::kIdle;
    resetTouch();
    if (g_backendReady) {
        ImGui_ImplOpenGL3_Shutdown();
        g_backendReady = false;
    }
    LOGI("Overlay detached");
}
