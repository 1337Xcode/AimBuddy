#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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
    original.showDetectionArea = false;
    original.showAimReach = false;
    original.assistEnabled = true;
    original.aimZone = 1;
    original.aimStrength = 0.8f;
    original.aimAfterLanding = 1;
    original.aimSwitching = 2;
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
    CHECK(!loaded.showDetectionArea && !loaded.showAimReach);
    CHECK(!loaded.assistEnabled);   // the assist is never saved: it starts off every run
    CHECK(loaded.aimZone == 1);
    CHECK_NEAR(loaded.aimStrength, 0.8, 1e-6);
    CHECK(loaded.aimAfterLanding == 1);
    CHECK(loaded.aimSwitching == 2);
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
    CHECK(loaded.showDetectionArea && loaded.showAimReach);   // a file from before the outlines existed shows them
}

TEST(settings_are_clamped_when_read) {
    const Settings loaded = SettingsStore::parse(
        "aimStrength=7\nconfidence=-3\nboxThickness=99\nsceneGain=0\nlanguage=42\nanchorRadius=9999\naimAfterLanding=5\naimSwitching=-1\n");
    CHECK_NEAR(loaded.aimStrength, 1.0, 1e-6);
    CHECK_NEAR(loaded.confidence, 0.1, 1e-6);
    CHECK(loaded.boxThickness == 6);
    CHECK_NEAR(loaded.sceneGain, 0.1, 1e-6);
    CHECK(loaded.language == 2);
    CHECK_NEAR(loaded.anchorRadius, 500.0, 1e-6);
    CHECK(loaded.aimAfterLanding == 1);
    CHECK(loaded.aimSwitching == 0);
}

TEST(a_value_that_is_not_a_number_keeps_the_default_when_read) {
    // Text, or nothing at all, after the sign: reading it as zero would mean 10% confidence and a sensitivity of 0.1.
    const Settings defaults;
    const Settings loaded = SettingsStore::parse("sceneGain=\nconfidence=abc\nshowLabels=true\nboxThickness=\naimStrength=0.9\n");
    CHECK_NEAR(loaded.sceneGain, defaults.sceneGain, 1e-6);
    CHECK_NEAR(loaded.confidence, defaults.confidence, 1e-6);
    CHECK(loaded.showLabels == defaults.showLabels);
    CHECK(loaded.boxThickness == defaults.boxThickness);
    CHECK_NEAR(loaded.aimStrength, 0.9, 1e-6);   // the good line next to the bad ones is still read
}

TEST(a_number_that_is_not_finite_is_refused) {
    // NaN passes every clamp, and would reach the aim loop's finger position.
    const Settings defaults;
    const Settings loaded = SettingsStore::parse("anchorX=nan\nconfidence=inf\naimStrength=-inf\naimFovRadius=NaN\n");
    CHECK_NEAR(loaded.anchorX, defaults.anchorX, 1e-6);
    CHECK_NEAR(loaded.confidence, defaults.confidence, 1e-6);
    CHECK_NEAR(loaded.aimStrength, defaults.aimStrength, 1e-6);
    CHECK_NEAR(loaded.aimFovRadius, defaults.aimFovRadius, 1e-6);

    CHECK_NEAR(SettingsStore::withValue(defaults, "anchorY", "nan").anchorY, defaults.anchorY, 1e-6);
    CHECK_NEAR(SettingsStore::withValue(defaults, "aimStrength", "inf").aimStrength, defaults.aimStrength, 1e-6);
}

TEST(an_integer_too_big_for_its_type_is_clamped_not_wrapped) {
    const Settings loaded = SettingsStore::parse("boxThickness=4294967297\nlanguage=4294967298\ntouchBackend=-4294967297\n");
    CHECK(loaded.boxThickness == 6);   // not 1
    CHECK(loaded.language == 2);       // not 2 by luck of the wrap: the largest value
    CHECK(loaded.touchBackend == 0);   // not 2147483647 wrapped back to a valid one
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

TEST(saves_from_several_threads_never_leave_half_a_file) {
    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/aimbuddy-settings-race.cfg";
    std::remove(path.c_str());
    SettingsStore store;
    store.saveIfChanged(path);   // nothing changed yet, so nothing is written
    Settings first = store.get();
    first.language = 1;
    store.set(first);
    CHECK(store.saveIfChanged(path));

    std::atomic<bool> writing{true};
    std::atomic<int> halfFiles{0};
    std::thread reader([&] {
        while (writing.load()) {
            std::ifstream file(path);
            std::stringstream text;
            text << file.rdbuf();
            // The last setting in the file is the language: a file that stops short lacks it.
            if (text.str().find("language=") == std::string::npos) {
                ++halfFiles;
            }
        }
    });
    std::vector<std::thread> writers;
    for (int writer = 0; writer < 3; ++writer) {
        writers.emplace_back([&, writer] {
            for (int round = 0; round < 300; ++round) {
                Settings settings = store.get();
                settings.aimStrength = 0.1f + 0.001f * static_cast<float>(round + writer);
                store.set(settings);
                store.saveIfChanged(path);
            }
        });
    }
    for (std::thread& thread : writers) {
        thread.join();
    }
    writing = false;
    reader.join();

    CHECK(halfFiles.load() == 0);
    std::remove(path.c_str());   // a host whose rename does not replace a file (Windows) needs the way cleared
    store.set(store.get());
    CHECK(store.saveIfChanged(path));
    SettingsStore reloaded;
    CHECK(reloaded.load(path));
    CHECK(reloaded.get().language == 1);
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
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

TEST(the_touch_area_stays_inside_the_display) {
    Settings nearTheEdge;
    nearTheEdge.anchorX = 0.95f;   // 120 px from the right edge of a display 2400 px wide
    nearTheEdge.anchorY = 0.5f;
    nearTheEdge.anchorRadius = 250.0f;
    const TouchArea squeezed = nearTheEdge.touchAreaOn(2400.0f, 1080.0f);
    CHECK_NEAR(squeezed.centerX, 2280.0, 1e-3);
    CHECK_NEAR(squeezed.centerY, 540.0, 1e-3);
    CHECK_NEAR(squeezed.radiusPx, 120.0, 1e-3);

    Settings nearTheTop = nearTheEdge;
    nearTheTop.anchorX = 0.5f;
    nearTheTop.anchorY = 0.05f;   // 54 px from the top edge of a display 1080 px high
    CHECK_NEAR(nearTheTop.touchAreaOn(2400.0f, 1080.0f).radiusPx, 54.0, 1e-3);

    Settings roomy;
    roomy.anchorX = 0.75f;
    roomy.anchorY = 0.4f;
    roomy.anchorRadius = 250.0f;
    const TouchArea whole = roomy.touchAreaOn(2400.0f, 1080.0f);
    CHECK_NEAR(whole.centerX, 1800.0, 1e-3);
    CHECK_NEAR(whole.centerY, 432.0, 1e-3);
    CHECK_NEAR(whole.radiusPx, 250.0, 1e-3);   // it fits already

    const TouchArea unknown = roomy.touchAreaOn(0.0f, 0.0f);
    CHECK_NEAR(unknown.radiusPx, 250.0, 1e-3);   // nothing is known about the display, so nothing is cut
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
