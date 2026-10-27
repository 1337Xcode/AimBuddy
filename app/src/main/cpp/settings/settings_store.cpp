#include "settings_store.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace aimbuddy {

namespace {

int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/**
 * Calls `visit(name, field)` for every setting. The one list drives both reading and writing, so a
 * setting added here is saved and loaded without touching anything else.
 *
 * `assistEnabled` is left out on purpose: the assist moves the aim by itself, so it starts off
 * every time the app runs and the player turns it on while in the game.
 */
template <typename Settings_, typename Visitor>
void visitFields(Settings_& s, Visitor&& visit) {
    visit("boxColorR", s.boxColor[0]);
    visit("boxColorG", s.boxColor[1]);
    visit("boxColorB", s.boxColor[2]);
    visit("boxThickness", s.boxThickness);
    visit("showLabels", s.showLabels);
    visit("showAimPoint", s.showAimPoint);
    visit("showSnapLine", s.showSnapLine);
    visit("showCount", s.showCount);
    visit("showPerformance", s.showPerformance);
    visit("confidence", s.confidence);
    visit("detectionZoneRadius", s.detectionZoneRadius);
    visit("aimZone", s.aimZone);
    visit("aimStrength", s.aimStrength);
    visit("aimFovRadius", s.aimFovRadius);
    visit("aimPriority", s.aimPriority);
    visit("autoCalibrate", s.autoCalibrate);
    visit("sceneGain", s.sceneGain);
    visit("actuationDelayMs", s.actuationDelayMs);
    visit("invertY", s.invertY);
    visit("learnedGainX", s.learnedGainX);
    visit("learnedGainY", s.learnedGainY);
    visit("learnedDelayMs", s.learnedDelayMs);
    visit("learnedConfidence", s.learnedConfidence);
    visit("anchorX", s.anchorX);
    visit("anchorY", s.anchorY);
    visit("anchorRadius", s.anchorRadius);
    visit("showAnchor", s.showAnchor);
    visit("anchorOpacity", s.anchorOpacity);
    visit("touchBackend", s.touchBackend);
    visit("streamerMode", s.streamerMode);
    visit("language", s.language);
}

/// Appends `name=value` for each field it is given.
struct Writer {
    std::ostringstream* out;
    void operator()(const char* name, float value) const { *out << name << '=' << value << '\n'; }
    void operator()(const char* name, int value) const { *out << name << '=' << value << '\n'; }
    void operator()(const char* name, bool value) const { *out << name << '=' << (value ? 1 : 0) << '\n'; }
};

/// Sets each field from the matching entry of a parsed file, leaving it alone when there is none.
struct Reader {
    const std::map<std::string, std::string>* values;

    const std::string* find(const char* name) const {
        const auto it = values->find(name);
        return it == values->end() ? nullptr : &it->second;
    }
    void operator()(const char* name, float& field) const {
        if (const std::string* text = find(name)) {
            field = std::strtof(text->c_str(), nullptr);
        }
    }
    void operator()(const char* name, int& field) const {
        if (const std::string* text = find(name)) {
            field = static_cast<int>(std::strtol(text->c_str(), nullptr, 10));
        }
    }
    void operator()(const char* name, bool& field) const {
        if (const std::string* text = find(name)) {
            field = std::strtol(text->c_str(), nullptr, 10) != 0;
        }
    }
};

/// Sets the one field whose name matches, from text. Text that is not a number changes nothing.
struct Setter {
    const std::string* name;
    const std::string* value;

    void operator()(const char* fieldName, float& field) const {
        if (*name != fieldName) {
            return;
        }
        char* end = nullptr;
        const float parsed = std::strtof(value->c_str(), &end);
        if (end != value->c_str()) {
            field = parsed;
        }
    }
    void operator()(const char* fieldName, int& field) const {
        if (*name != fieldName) {
            return;
        }
        char* end = nullptr;
        const long parsed = std::strtol(value->c_str(), &end, 10);
        if (end != value->c_str()) {
            field = static_cast<int>(parsed);
        }
    }
    void operator()(const char* fieldName, bool& field) const {
        int number = field ? 1 : 0;
        (*this)(fieldName, number);
        field = number != 0;
    }
};

float clampTo(float value, float low, float high) { return std::clamp(value, low, high); }

}  // namespace

void Settings::clamp() {
    for (float& channel : boxColor) {
        channel = clampTo(channel, 0.0f, 1.0f);
    }
    boxThickness = std::clamp(boxThickness, 1, 6);
    confidence = clampTo(confidence, 0.1f, 0.95f);
    detectionZoneRadius = clampTo(detectionZoneRadius, 100.0f, 650.0f);
    aimZone = std::clamp(aimZone, 0, 2);
    aimStrength = clampTo(aimStrength, 0.0f, 1.0f);
    aimFovRadius = clampTo(aimFovRadius, 50.0f, 650.0f);
    aimPriority = std::clamp(aimPriority, 0, 2);
    sceneGain = clampTo(sceneGain, 0.1f, 8.0f);
    actuationDelayMs = clampTo(actuationDelayMs, 0.0f, 250.0f);
    learnedGainX = learnedGainX > 0.0f ? clampTo(learnedGainX, 0.15f, 40.0f) : 0.0f;
    learnedGainY = learnedGainY > 0.0f ? clampTo(learnedGainY, 0.15f, 40.0f) : 0.0f;
    learnedDelayMs = clampTo(learnedDelayMs, 0.0f, 250.0f);
    learnedConfidence = clampTo(learnedConfidence, 0.0f, 1.0f);
    anchorX = clampTo(anchorX, 0.05f, 0.95f);
    anchorY = clampTo(anchorY, 0.05f, 0.95f);
    anchorRadius = clampTo(anchorRadius, 50.0f, 500.0f);
    anchorOpacity = clampTo(anchorOpacity, 0.1f, 1.0f);
    touchBackend = std::clamp(touchBackend, 0, 2);
    language = std::clamp(language, 0, 2);
}

Settings Settings::resetKeepingLanguage() const {
    Settings fresh;
    fresh.language = language;
    return fresh;
}

Settings SettingsStore::withValue(const Settings& settings, const std::string& name, const std::string& value) {
    Settings changed = settings;
    visitFields(changed, Setter{&name, &value});
    changed.clamp();
    return changed;
}

std::string SettingsStore::serialize(const Settings& settings) {
    std::ostringstream out;
    out.precision(7);
    Settings copy = settings;
    visitFields(copy, Writer{&out});
    return out.str();
}

Settings SettingsStore::parse(const std::string& text) {
    std::map<std::string, std::string> values;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            continue;
        }
        std::string value = line.substr(equals + 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) {
            value.pop_back();
        }
        values[line.substr(0, equals)] = value;
    }
    Settings settings;
    visitFields(settings, Reader{&values});
    settings.clamp();
    return settings;
}

Settings SettingsStore::get() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

void SettingsStore::set(const Settings& settings) {
    Settings clamped = settings;
    clamped.clamp();
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = clamped;
    dirty_ = true;
    changedAtNs_ = steadyNowNs();
}

bool SettingsStore::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    const Settings loaded = parse(text.str());
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = loaded;
    dirty_ = false;
    return true;
}

bool SettingsStore::hasUnsavedChanges(int64_t* changedAtNs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (changedAtNs != nullptr) {
        *changedAtNs = changedAtNs_;
    }
    return dirty_;
}

bool SettingsStore::saveIfChanged(const std::string& path) {
    std::lock_guard<std::mutex> saving(saveMutex_);
    std::string text;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dirty_) {
            return true;
        }
        text = serialize(settings_);
        dirty_ = false;   // cleared first: a change made while writing sets it again
    }
    // Written beside the real file and moved into place, so a crash never leaves half a file.
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        file << text;
        if (!file) {
            std::lock_guard<std::mutex> lock(mutex_);
            dirty_ = true;
            return false;
        }
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        dirty_ = true;
        return false;
    }
    return true;
}

}  // namespace aimbuddy
