#ifndef AIMBUDDY_SETTINGS_SETTINGS_STORE_H
#define AIMBUDDY_SETTINGS_SETTINGS_STORE_H

#include <cstdint>
#include <mutex>
#include <string>

#include "settings.h"

namespace aimbuddy {

/**
 * The one place the player's settings live.
 *
 * Any thread may read or replace them: a reader gets its own copy, so it never sees a change
 * half made. The file format is one `name=value` line per setting. Readers ignore names they do
 * not know and keep the default for names that are missing, so a settings file from another
 * version of the app still loads.
 */
class SettingsStore {
public:
    /// A copy of the current settings.
    Settings get() const;

    /// Replaces the settings (values are clamped) and notes that they need saving.
    void set(const Settings& settings);

    /// Reads the file. A missing or unreadable file leaves the defaults and returns false.
    bool load(const std::string& path);

    /// Writes the file if anything changed since the last write. Returns false only if writing failed.
    bool saveIfChanged(const std::string& path);

    /// True if the settings changed since they were last written, and for how long (steady clock, ns).
    bool hasUnsavedChanges(int64_t* changedAtNs = nullptr) const;

    /// The file's text for a set of settings; the parts of load() and save that do not touch the disk.
    static std::string serialize(const Settings& settings);
    static Settings parse(const std::string& text);

private:
    mutable std::mutex mutex_;
    Settings settings_;
    bool dirty_ = false;
    int64_t changedAtNs_ = 0;
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_SETTINGS_SETTINGS_STORE_H
