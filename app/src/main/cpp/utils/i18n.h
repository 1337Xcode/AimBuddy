/**
 * i18n.h - The words of the overlay menu in every language it speaks.
 *
 * To add a language: add it to Language, give it a name in LanguageName(), and add a column to
 * kTranslations. A missing entry shows the English one, so a half finished translation is still
 * readable. If the language needs glyphs the menu's fonts do not have, add a font that does in
 * renderer/imgui_menu.cpp (loadFonts).
 *
 * Source files are UTF-8, so the translations are written as they read.
 */
#ifndef AIMBUDDY_I18N_H
#define AIMBUDDY_I18N_H

#include <atomic>

// AIMBUDDY_VERSION is injected by the Gradle CMake configuration (see app/build.gradle).
// The fallback keeps standalone IDE indexing happy.
#ifndef AIMBUDDY_VERSION
#define AIMBUDDY_VERSION "0.0.0-dev"
#endif

namespace aimbuddy::i18n {

enum class Language : int {
    English = 0,
    Chinese = 1,
    Russian = 2,
    Count = 3,
};

inline std::atomic<int> g_currentLanguage{static_cast<int>(Language::English)};

inline void SetLanguage(int language) {
    if (language >= 0 && language < static_cast<int>(Language::Count)) {
        g_currentLanguage.store(language, std::memory_order_relaxed);
    }
}

inline int GetLanguage() { return g_currentLanguage.load(std::memory_order_relaxed); }

/// A language's own name, for the language picker.
inline const char* LanguageName(int language) {
    switch (static_cast<Language>(language)) {
        case Language::Chinese: return "中文";
        case Language::Russian: return "Русский";
        default: return "English";
    }
}

enum class Key : int {
    // Window and tabs
    AppTitle,
    TabVisuals,
    TabAim,
    TabInfo,
    Close,
    StopService,

    // Visuals tab
    ShowLabels,
    ShowSnapLine,
    ShowAimPoint,
    ShowDetectionArea,
    ShowAimReach,
    ShowCount,
    ShowPerformance,
    BoxColor,
    BoxThickness,
    Confidence,
    DetectionZone,
    ShowAnchor,
    AnchorOpacity,
    StreamerMode,
    Language,

    // Aim tab
    AssistEnable,
    TouchAccess,
    AccessAutomatic,
    AccessShizuku,
    AccessRoot,
    HelperOff,
    HelperIdle,
    HelperWaiting,
    HelperActive,
    ReasonNoTouchscreen,
    ReasonDenied,
    ReasonVirtualDenied,
    ReasonGrab,
    ReasonHeartbeat,
    ReasonEscape,
    AimZone,
    ZoneHead,
    ZoneChest,
    ZoneCentre,
    AimStrength,
    AimRadius,
    AimPriority,
    PriorityNearest,
    PriorityLargest,
    PriorityCertain,
    AimAfterLanding,
    AfterKeepFollowing,
    AfterOneCorrection,
    AimSwitching,
    SwitchSticky,
    SwitchBalanced,
    SwitchQuick,
    CalibrationTitle,
    CalibrationHint,
    CameraSensitivity,
    ResponseDelay,
    InvertVertical,
    TouchAreaTitle,
    AnchorX,
    AnchorY,
    AnchorSize,
    ResetTouchArea,
    MoveTouchArea,
    FineTune,
    ZoneEditorHint,
    Done,
    NotInStreamerMode,

    // Colour detection
    DetectColour,
    ColourNotLearned,
    ColourLearned,
    LearnColour,
    LearnColourHint,
    PickTheColour,
    Cancel,
    LearnFound,
    LearnNotFound,
    ColourTolerance,
    ColourMinHeight,

    // Info tab
    InfoTitle,
    InfoVersion,
    InfoOverlayFps,
    InfoDetectionsPerSecond,
    InfoDelay,
    InfoTargets,
    InfoScreen,
    InfoTip,
    ResetAllSettings,
    ResetAllConfirm,

    Count,
};

// Rows are keys, columns are languages: kTranslations[key][language].
inline const char* kTranslations[static_cast<int>(Key::Count)][static_cast<int>(Language::Count)] = {
    // English | 中文 | Русский
    /* AppTitle */             {"AimBuddy v" AIMBUDDY_VERSION, "AimBuddy v" AIMBUDDY_VERSION, "AimBuddy v" AIMBUDDY_VERSION},
    /* TabVisuals */           {"Visuals", "视觉", "Визуал"},
    /* TabAim */               {"Aim assist", "瞄准辅助", "Помощь"},
    /* TabInfo */              {"Info", "信息", "Инфо"},
    /* Close */                {"Close", "关闭", "Закрыть"},
    /* StopService */          {"Stop AimBuddy", "停止 AimBuddy", "Остановить AimBuddy"},

    /* ShowLabels */           {"Confidence labels", "置信度标签", "Метки уверенности"},
    /* ShowSnapLine */         {"Line to nearest target", "指向最近目标的连线", "Линия к ближайшей цели"},
    /* ShowAimPoint */         {"Aim point marker", "瞄准点标记", "Метка точки прицела"},
    /* ShowDetectionArea */    {"Detection area outline", "检测区域轮廓", "Контур области обнаружения"},
    /* ShowAimReach */         {"Aim reach circle", "瞄准范围圈", "Круг дальности прицеливания"},
    /* ShowCount */            {"Target count", "目标数量", "Число целей"},
    /* ShowPerformance */      {"Performance readout", "性能信息", "Показатели работы"},
    /* BoxColor */             {"Box colour", "框颜色", "Цвет рамки"},
    /* BoxThickness */         {"Box thickness", "框线宽", "Толщина рамки"},
    /* Confidence */           {"Detection confidence", "检测置信度", "Порог уверенности"},
    /* DetectionZone */        {"Detection zone", "检测区域", "Зона обнаружения"},
    /* ShowAnchor */           {"Show touch area", "显示触控区域", "Показать зону касания"},
    /* AnchorOpacity */        {"Touch area opacity", "触控区域透明度", "Прозрачность зоны"},
    /* StreamerMode */         {"Streamer mode", "主播模式", "Режим стримера"},
    /* Language */             {"Language", "语言", "Язык"},

    /* AssistEnable */         {"Enable aim assist", "启用瞄准辅助", "Включить помощь"},
    /* TouchAccess */          {"Touch access", "触控方式", "Доступ к касаниям"},
    /* AccessAutomatic */      {"Automatic", "自动", "Автоматически"},
    /* AccessShizuku */        {"Shizuku", "Shizuku", "Shizuku"},
    /* AccessRoot */           {"Root", "Root", "Root"},
    /* HelperOff */            {"Touch helper: not running", "触控助手：未运行", "Помощник: не запущен"},
    /* HelperIdle */           {"Touch helper: ready", "触控助手：就绪", "Помощник: готов"},
    /* HelperWaiting */        {"Lift your fingers off the screen to start", "请抬起手指以开始", "Уберите пальцы с экрана, чтобы начать"},
    /* HelperActive */         {"Touch helper: active", "触控助手：已启用", "Помощник: активен"},
    /* ReasonNoTouchscreen */  {"No touchscreen was found", "未找到触摸屏", "Сенсорный экран не найден"},
    /* ReasonDenied */         {"No permission to read the touchscreen", "没有读取触摸屏的权限", "Нет доступа к сенсорному экрану"},
    /* ReasonVirtualDenied */  {"A virtual touchscreen could not be created", "无法创建虚拟触摸屏", "Не удалось создать виртуальный экран"},
    /* ReasonGrab */           {"Another program holds the touchscreen", "触摸屏被其他程序占用", "Экран занят другой программой"},
    /* ReasonHeartbeat */      {"The touch helper stopped answering", "触控助手无响应", "Помощник перестал отвечать"},
    /* ReasonEscape */         {"Stopped with the volume keys", "已通过音量键停止", "Остановлено кнопками громкости"},
    /* AimZone */              {"Aim at", "瞄准部位", "Цель"},
    /* ZoneHead */             {"Head", "头部", "Голова"},
    /* ZoneChest */            {"Chest", "胸部", "Грудь"},
    /* ZoneCentre */           {"Centre", "中心", "Центр"},
    /* AimStrength */          {"Strength", "强度", "Сила"},
    /* AimRadius */            {"Target radius", "目标范围", "Радиус целей"},
    /* AimPriority */          {"Pick target by", "选择目标", "Выбор цели"},
    /* PriorityNearest */      {"Closest to crosshair", "最靠近准星", "Ближе к прицелу"},
    /* PriorityLargest */      {"Biggest on screen", "屏幕上最大", "Самая крупная"},
    /* PriorityCertain */      {"Most certain", "最可信", "Самая уверенная"},
    /* AimAfterLanding */      {"Once on target", "到达目标后", "После наведения"},
    /* AfterKeepFollowing */   {"Keep following", "持续跟随", "Следить"},
    /* AfterOneCorrection */   {"One correction", "只修正一次", "Один раз"},
    /* AimSwitching */         {"Switching targets", "切换目标", "Смена цели"},
    /* SwitchSticky */         {"Sticky", "稳定", "Держать"},
    /* SwitchBalanced */       {"Balanced", "均衡", "Баланс"},
    /* SwitchQuick */          {"Quick", "快速", "Быстро"},
    /* CalibrationTitle */     {"Game calibration", "游戏校准", "Калибровка игры"},
    /* CalibrationHint */      {"These two numbers describe how your game reacts to a finger. A wrong value makes the aim swing past the target or crawl.",
                                "这两个数值描述游戏对手指的反应。数值不准会使准星冲过目标或移动缓慢。",
                                "Эти два числа описывают, как игра реагирует на палец. Неверные значения заставляют прицел перелетать цель или ползти."},
    /* CameraSensitivity */    {"Camera sensitivity", "视角灵敏度", "Чувствительность камеры"},
    /* ResponseDelay */        {"Response delay", "响应延迟", "Задержка отклика"},
    /* InvertVertical */       {"Invert vertical", "反转垂直方向", "Инвертировать вертикаль"},
    /* TouchAreaTitle */       {"Touch area", "触控区域", "Зона касания"},
    /* AnchorX */              {"Position across", "水平位置", "Позиция по горизонтали"},
    /* AnchorY */              {"Position down", "垂直位置", "Позиция по вертикали"},
    /* AnchorSize */           {"Size", "大小", "Размер"},
    /* ResetTouchArea */       {"Reset touch area", "重置触控区域", "Сбросить зону касания"},
    /* MoveTouchArea */        {"Move or resize on screen", "在屏幕上移动或调整大小", "Переместить или изменить размер на экране"},
    /* FineTune */             {"Fine tune", "微调", "Точная настройка"},
    /* ZoneEditorHint */       {"Drag the circle to move it. Drag the dot to resize it.",
                                "拖动圆圈来移动，拖动圆点来调整大小。",
                                "Перетащите круг, чтобы переместить. Перетащите точку, чтобы изменить размер."},
    /* Done */                 {"Done", "完成", "Готово"},
    /* NotInStreamerMode */    {"Not available in streamer mode, which draws nothing over the game. Turn it off to use this.",
                                "主播模式不会在游戏画面上绘制任何内容，所以这里不可用。关闭主播模式后即可使用。",
                                "Недоступно в режиме стримера: он ничего не рисует поверх игры. Отключите его, чтобы пользоваться этим."},

    /* DetectColour */         {"Colour detection", "颜色检测", "Поиск по цвету"},
    /* ColourNotLearned */     {"No colour learned yet", "尚未学习颜色", "Цвет ещё не выбран"},
    /* ColourLearned */        {"Colour learned", "已学习颜色", "Цвет выбран"},
    /* LearnColour */          {"Learn colour", "学习颜色", "Запомнить цвет"},
    /* LearnColourHint */      {"Press the button, then tap the colour you want inside the blue square, such as an enemy's outline. The menu closes while you pick.",
                                "点击按钮后，在蓝色方框内点按想要的颜色，例如敌人的轮廓。选取时菜单会关闭。",
                                "Нажмите кнопку, затем коснитесь нужного цвета внутри синего квадрата, например контура противника. Меню закроется на время выбора."},
    /* PickTheColour */        {"Tap the colour inside the blue square", "在蓝色方框内点按该颜色", "Коснитесь цвета внутри синего квадрата"},
    /* Cancel */               {"Cancel", "取消", "Отмена"},
    /* LearnFound */           {"Colour learned", "已学习颜色", "Цвет запомнен"},
    /* LearnNotFound */        {"No vivid colour there", "那里没有鲜艳的颜色", "Там нет яркого цвета"},
    /* ColourTolerance */      {"Colour tolerance", "颜色容差", "Допуск цвета"},
    /* ColourMinHeight */      {"Smallest target", "最小目标", "Наименьшая цель"},

    /* InfoTitle */            {"AimBuddy (open source)", "AimBuddy(开源)", "AimBuddy (открытый код)"},
    /* InfoVersion */          {"Version: " AIMBUDDY_VERSION, "版本：" AIMBUDDY_VERSION, "Версия: " AIMBUDDY_VERSION},
    /* InfoOverlayFps */       {"Overlay frames per second", "悬浮层帧率", "Кадров оверлея в секунду"},
    /* InfoDetectionsPerSecond */ {"Detections per second", "每秒检测次数", "Обнаружений в секунду"},
    /* InfoDelay */            {"Delay from screen to result", "画面到结果的延迟", "Задержка от экрана до результата"},
    /* InfoTargets */          {"Targets in view", "视野内目标", "Целей в поле зрения"},
    /* InfoScreen */           {"Screen", "屏幕", "Экран"},
    /* InfoTip */              {"Tip: start with the defaults, then change one setting at a time.",
                                "提示：先使用默认设置，再逐项调整。",
                                "Совет: начните с настроек по умолчанию и меняйте по одной."},
    /* ResetAllSettings */     {"Reset all settings", "重置所有设置", "Сбросить все настройки"},
    /* ResetAllConfirm */      {"Tap again to reset", "再次点击以重置", "Нажмите ещё раз для сброса"},
};

/// The text for a key in the current language; English when that language has none.
inline const char* T(Key key) {
    int language = GetLanguage();
    if (language < 0 || language >= static_cast<int>(Language::Count)) {
        language = 0;
    }
    const int index = static_cast<int>(key);
    if (index < 0 || index >= static_cast<int>(Key::Count)) {
        return "";
    }
    const char* text = kTranslations[index][language];
    if (text == nullptr || *text == '\0') {
        text = kTranslations[index][0];
    }
    return text != nullptr ? text : "";
}

}  // namespace aimbuddy::i18n

#endif  // AIMBUDDY_I18N_H
