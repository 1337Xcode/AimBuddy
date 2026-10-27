#include <cstdio>
#include <string>

#include "settings/settings_store.h"
#include "test_harness.h"

using namespace aimbuddy;

TEST(settings_survive_a_round_trip) {
    Settings original;
    original.boxColor[0] = 0.25f;
    original.boxColor[1] = 0.5f;
    original.boxColor[2] = 0.75f;
    original.boxThickness = 4;
    original.showSnapLine = true;
    original.showAimPoint = false;
    original.assistEnabled = true;
    original.aimZone = 1;
    original.aimStrength = 0.8f;
    original.sceneGain = 2.25f;
    original.actuationDelayMs = 72.0f;
    original.invertY = true;
    original.anchorX = 0.6f;
    original.touchBackend = 2;
    original.language = 2;

    const Settings loaded = SettingsStore::parse(SettingsStore::serialize(original));
    CHECK_NEAR(loaded.boxColor[0], 0.25, 1e-6);
    CHECK_NEAR(loaded.boxColor[2], 0.75, 1e-6);
    CHECK(loaded.boxThickness == 4);
    CHECK(loaded.showSnapLine && !loaded.showAimPoint);
    CHECK(!loaded.assistEnabled);   // the assist is never saved: it starts off every run
    CHECK(loaded.aimZone == 1);
    CHECK_NEAR(loaded.aimStrength, 0.8, 1e-6);
    CHECK_NEAR(loaded.sceneGain, 2.25, 1e-6);
    CHECK_NEAR(loaded.actuationDelayMs, 72.0, 1e-4);
    CHECK(loaded.invertY);
    CHECK_NEAR(loaded.anchorX, 0.6, 1e-6);
    CHECK(loaded.touchBackend == 2);
    CHECK(loaded.language == 2);
}

TEST(settings_keep_defaults_for_missing_and_unknown_names) {
    // A file from another version: one name this version does not have, and most of its own missing.
    const Settings loaded = SettingsStore::parse("aimStrength=0.9\nsomethingFromTheFuture=7\n");
    const Settings defaults;
    CHECK_NEAR(loaded.aimStrength, 0.9, 1e-6);
    CHECK_NEAR(loaded.confidence, defaults.confidence, 1e-6);
    CHECK(loaded.boxThickness == defaults.boxThickness);
    CHECK(loaded.language == defaults.language);
}

TEST(settings_are_clamped_when_read) {
    const Settings loaded = SettingsStore::parse(
        "aimStrength=7\nconfidence=-3\nboxThickness=99\nsceneGain=0\nlanguage=42\nanchorRadius=9999\n");
    CHECK_NEAR(loaded.aimStrength, 1.0, 1e-6);
    CHECK_NEAR(loaded.confidence, 0.1, 1e-6);
    CHECK(loaded.boxThickness == 6);
    CHECK_NEAR(loaded.sceneGain, 0.1, 1e-6);
    CHECK(loaded.language == 2);
    CHECK_NEAR(loaded.anchorRadius, 500.0, 1e-6);
}

TEST(settings_tolerate_damaged_lines) {
    const Settings loaded = SettingsStore::parse("\n=5\nnonsense\r\nconfidence=0.7\r\n  \n");
    CHECK_NEAR(loaded.confidence, 0.7, 1e-6);
}

TEST(store_hands_out_copies_and_tracks_changes) {
    SettingsStore store;
    CHECK(!store.hasUnsavedChanges());
    Settings changed = store.get();
    changed.assistEnabled = true;
    changed.aimStrength = 5.0f;   // out of range
    store.set(changed);
    CHECK(store.hasUnsavedChanges());
    CHECK(store.get().assistEnabled);
    CHECK_NEAR(store.get().aimStrength, 1.0, 1e-6);   // stored clamped

    changed.assistEnabled = false;   // editing the copy does not change the store
    CHECK(store.get().assistEnabled);
}

TEST(store_writes_and_reads_a_file) {
    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/aimbuddy-settings-test.cfg";
    std::remove(path.c_str());

    SettingsStore writer;
    CHECK(!writer.load(path));   // nothing there yet
    Settings settings = writer.get();
    settings.language = 1;
    settings.aimFovRadius = 333.0f;
    writer.set(settings);
    CHECK(writer.saveIfChanged(path));
    CHECK(!writer.hasUnsavedChanges());

    SettingsStore reader;
    CHECK(reader.load(path));
    CHECK(reader.get().language == 1);
    CHECK_NEAR(reader.get().aimFovRadius, 333.0, 1e-4);
    CHECK(!reader.hasUnsavedChanges());
    std::remove(path.c_str());
}

TEST(a_single_setting_can_be_changed_by_its_file_name) {
    const Settings original;
    Settings changed = SettingsStore::withValue(original, "aimStrength", "0.25");
    CHECK_NEAR(changed.aimStrength, 0.25, 1e-6);
    changed = SettingsStore::withValue(changed, "showSnapLine", "1");
    CHECK(changed.showSnapLine);
    changed = SettingsStore::withValue(changed, "boxThickness", "4");
    CHECK(changed.boxThickness == 4);
    CHECK_NEAR(changed.aimStrength, 0.25, 1e-6);   // the earlier changes stay

    // Values stay in range, and names or numbers that mean nothing change nothing.
    CHECK_NEAR(SettingsStore::withValue(original, "aimStrength", "9").aimStrength, 1.0, 1e-6);
    CHECK_NEAR(SettingsStore::withValue(original, "noSuchSetting", "1").aimStrength, original.aimStrength, 1e-6);
    CHECK_NEAR(SettingsStore::withValue(original, "aimStrength", "abc").aimStrength, original.aimStrength, 1e-6);
}

TEST(resetting_keeps_only_the_language) {
    Settings changed;
    changed.language = 2;
    changed.aimStrength = 0.9f;
    changed.anchorRadius = 120.0f;
    changed.showSnapLine = true;
    changed.touchBackend = 2;

    const Settings fresh = changed.resetKeepingLanguage();
    const Settings defaults;
    CHECK(fresh.language == 2);
    CHECK_NEAR(fresh.aimStrength, defaults.aimStrength, 1e-6);
    CHECK_NEAR(fresh.anchorRadius, defaults.anchorRadius, 1e-6);
    CHECK(!fresh.showSnapLine);
    CHECK(fresh.touchBackend == defaults.touchBackend);
}

TEST(what_was_learned_about_the_camera_is_kept_and_kept_in_range) {
    Settings original;
    original.autoCalibrate = false;
    original.learnedGainX = 4.5f;
    original.learnedGainY = 3.75f;
    original.learnedDelayMs = 64.0f;
    original.learnedConfidence = 0.8f;
    const Settings loaded = SettingsStore::parse(SettingsStore::serialize(original));
    CHECK(!loaded.autoCalibrate);
    CHECK_NEAR(loaded.learnedGainX, 4.5, 1e-6);
    CHECK_NEAR(loaded.learnedGainY, 3.75, 1e-6);
    CHECK_NEAR(loaded.learnedDelayMs, 64.0, 1e-4);
    CHECK_NEAR(loaded.learnedConfidence, 0.8, 1e-6);

    // Nothing learned is zero and stays zero; an absurd figure is brought into range.
    const Settings fresh = SettingsStore::parse("");
    CHECK(fresh.autoCalibrate);
    CHECK_NEAR(fresh.learnedGainX, 0.0, 1e-9);
    const Settings wild = SettingsStore::parse("learnedGainX=900\nlearnedGainY=0.001\nlearnedConfidence=7\n");
    CHECK_NEAR(wild.learnedGainX, 40.0, 1e-6);
    CHECK_NEAR(wild.learnedGainY, 0.15, 1e-6);
    CHECK_NEAR(wild.learnedConfidence, 1.0, 1e-6);
}
