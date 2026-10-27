#include <atomic>
#include <chrono>
#include <thread>

#include "core/frame_mailbox.h"
#include "test_harness.h"

using aimbuddy::LatestMailbox;

namespace {

/// A payload whose fields must always agree; a mismatch means the consumer saw a half-written value.
struct Stamped {
    uint64_t sequence = 0;
    uint64_t copy[64] = {};
};

}  // namespace

TEST(mailbox_returns_nothing_before_the_first_publish) {
    LatestMailbox<int> mailbox;
    CHECK(mailbox.waitForLatest(std::chrono::milliseconds(5)) == nullptr);
}

TEST(mailbox_keeps_only_the_newest_value) {
    LatestMailbox<int> mailbox;
    for (int value = 1; value <= 3; ++value) {
        mailbox.beginWrite() = value;
        mailbox.publish();
    }
    int* latest = mailbox.waitForLatest(std::chrono::milliseconds(5));
    CHECK(latest != nullptr);
    CHECK(latest != nullptr && *latest == 3);
    mailbox.endRead();
    CHECK(mailbox.overwrittenCount() == 2);
    CHECK(mailbox.waitForLatest(std::chrono::milliseconds(5)) == nullptr);
}

TEST(mailbox_never_hands_out_the_slot_being_read) {
    LatestMailbox<int> mailbox;
    mailbox.beginWrite() = 10;
    mailbox.publish();
    int* reading = mailbox.waitForLatest(std::chrono::milliseconds(5));
    CHECK(reading != nullptr);
    // While the consumer holds a slot, many writes must never touch it.
    for (int value = 11; value < 40; ++value) {
        int& slot = mailbox.beginWrite();
        CHECK(&slot != reading);
        slot = value;
        mailbox.publish();
    }
    CHECK(reading != nullptr && *reading == 10);
    mailbox.endRead();
}

TEST(mailbox_close_wakes_a_waiting_consumer) {
    LatestMailbox<int> mailbox;
    std::atomic<bool> returned{false};
    std::thread consumer([&] {
        mailbox.waitForLatest(std::chrono::seconds(5));
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mailbox.close();
    consumer.join();
    CHECK(returned.load());
}

TEST(mailbox_never_shows_a_torn_value_under_load) {
    LatestMailbox<Stamped> mailbox;
    std::atomic<bool> running{true};
    std::thread producer([&] {
        for (uint64_t n = 1; running.load(); ++n) {
            Stamped& slot = mailbox.beginWrite();
            slot.sequence = n;
            for (auto& field : slot.copy) {
                field = n;
            }
            mailbox.publish();
        }
    });

    uint64_t last = 0;
    int reads = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < deadline) {
        Stamped* value = mailbox.waitForLatest(std::chrono::milliseconds(10));
        if (value == nullptr) {
            continue;
        }
        for (const auto field : value->copy) {
            if (field != value->sequence) {
                CHECK(false && "torn value");
                break;
            }
        }
        CHECK(value->sequence > last);  // the consumer only ever moves forward
        last = value->sequence;
        mailbox.endRead();
        ++reads;
    }
    running = false;
    producer.join();
    CHECK(reads > 10);
}
