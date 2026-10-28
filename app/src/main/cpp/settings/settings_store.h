#ifndef AIMBUDDY_SETTINGS_SETTINGS_STORE_H
#define AIMBUDDY_SETTINGS_SETTINGS_STORE_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "settings.h"

namespace aimbuddy {

/**
 * The one place the player's settings live.
 *
 * Any thread may read or change them: a reader gets its own copy, so it never sees a change
 * half made, and update() and applyEdit() change them under the lock, so two threads that change
 * different settings at the same moment cannot undo one another. The file format is one
 * `name=value` line per setting. Readers ignore names they do not know and keep the default for
 * names that are missing, so a settings file from another version of the app still loads.
 */
class SettingsStore {
public:
    /// A copy of the current settings.
    Settings get() const;

    /// Replaces all the settings (values are clamped) and notes that they need saving.
    void set(const Settings& settings);

    /**
     * Changes the settings with `change`, which is handed them under the lock (so it must be quick and must not
     * call the store). Values are clamped afterwards and the settings are noted as needing to be saved.
     */
    void update(const std::function<void(Settings&)>& change);

    /**
     * Makes a change that was made to a copy of the settings: `before` is the copy as it was taken and `after`
     * what an edit made of it. Only what differs is written, onto the settings as they are now, so a setting
     * another thread changed in the meantime keeps its new value.
     */
    void applyEdit(const Settings& before, const Settings& after);

    /// Reads the file. A missing or unreadable file leaves the defaults and returns false. Changes made before
    /// this are not lost: they are made again on top of what the file says.
    bool load(const std::string& path);

    /// Writes the file if anything changed since the last write. Returns false only if writing failed.
    bool saveIfChanged(const std::string& path);

    /// True if the settings changed since they were last written, and for how long (steady clock, ns).
    bool hasUnsavedChanges(int64_t* changedAtNs = nullptr) const;

    /// The file's text for a set of settings; the parts of load() and save that do not touch the disk.
    static std::string serialize(const Settings& settings);
    static Settings parse(const std::string& text);

    /// `settings` with one setting changed by its file name, such as ("aimStrength", "0.7"). Unknown names change nothing.
    static Settings withValue(const Settings& settings, const std::string& name, const std::string& value);

private:
    /// Runs `change` on the settings under the lock, and until the file has been read keeps it to be run again after.
    void apply(const std::function<void(Settings&)>& change);

    mutable std::mutex mutex_;
    std::mutex saveMutex_;   ///< One save at a time, so two never write the same temporary file.
    Settings settings_;
    bool dirty_ = false;
    int64_t changedAtNs_ = 0;
    bool loaded_ = false;                                       ///< load() has run, whether or not there was a file.
    std::vector<std::function<void(Settings&)>> beforeLoad_;    ///< Changes made before that, in order.
};

}  // namespace aimbuddy

#endif  // AIMBUDDY_SETTINGS_SETTINGS_STORE_H
