#ifndef AIMBUDDY_CORE_TOUCH_PROTOCOL_H
#define AIMBUDDY_CORE_TOUCH_PROTOCOL_H

#include <cstdint>
#include <cstring>

namespace aimbuddy::touch::protocol {

/**
 * The messages between the app and the touch helper, a small program that runs with the
 * privileges needed to read the touchscreen and create virtual input devices.
 *
 * Every message is one fixed 16 byte packet, so a reader never has to deal with a message split
 * across reads. The three numbers a, b and c mean something different for each type.
 */

/// First number of a hello: tells the helper the other side speaks this protocol ("ABT1").
constexpr int32_t kHelloMagic = 0x31544241;
constexpr int32_t kVersion = 1;

/// Size of every packet in bytes.
constexpr int kPacketSize = 16;

/// What the app asks of the helper.
enum class Request : uint8_t {
    kHello = 1,      ///< a: kHelloMagic, b: kVersion. Must be the first packet.
    kToken = 2,      ///< a, b: the two halves of the secret token. Must be the second packet.
    kConfigure = 3,  ///< a: display width, b: display height, c: rotation (0 to 3), all in pixels / quarter turns.
    kEngage = 4,     ///< Take over the touchscreen so the extra finger can be added.
    kRelease = 5,    ///< Give the touchscreen back and lift the extra finger.
    kAimDown = 6,    ///< a, b: position (float bits) in display pixels.
    kAimMove = 7,    ///< a, b: position (float bits) in display pixels.
    kAimUp = 8,      ///< Lift the extra finger.
    kPing = 9,       ///< Heartbeat. The helper lets go of the touchscreen if these stop.
    kQuit = 10,      ///< Shut down.
};

/// What the helper tells the app.
enum class Report : uint8_t {
    kReady = 1,   ///< The touchscreen was found. a: slot count, b: raw x maximum, c: raw y maximum.
    kStatus = 2,  ///< state and reason describe where things stand.
};

/// Where the helper stands.
enum class State : uint8_t {
    kStarting = 0,
    kReleased = 1,        ///< The touchscreen is the system's again.
    kWaitingForFingers = 2,///< Engage was asked for but fingers are on the screen; waiting for them to lift.
    kMerging = 3,         ///< The extra finger can be used.
    kStopped = 4,         ///< The helper is shutting down.
};

/// Why the helper did what it did. Zero means nothing went wrong.
enum class Reason : uint16_t {
    kNone = 0,
    kNoTouchscreen = 1,       ///< No suitable touchscreen was found.
    kTouchscreenDenied = 2,   ///< The touchscreen could not be opened: no permission.
    kVirtualDeviceDenied = 3, ///< /dev/uinput could not be opened or used.
    kGrabFailed = 4,          ///< Another program holds the touchscreen.
    kDeviceLost = 5,          ///< The touchscreen went away.
    kHeartbeatLost = 6,       ///< The app stopped answering.
    kEscapeKeys = 7,          ///< The volume keys were held to take the screen back.
    kBadClient = 8,           ///< A connection without the right token or protocol.
    kRequested = 9,           ///< The app asked.
};

/// A request from the app to the helper.
struct RequestPacket {
    uint8_t type = 0;
    uint8_t reserved[3] = {0, 0, 0};
    int32_t a = 0;
    int32_t b = 0;
    int32_t c = 0;
};

/// A report from the helper to the app.
struct ReportPacket {
    uint8_t type = 0;
    uint8_t state = 0;
    uint16_t reason = 0;
    int32_t a = 0;
    int32_t b = 0;
    int32_t c = 0;
};

static_assert(sizeof(RequestPacket) == kPacketSize, "packets must keep their size");
static_assert(sizeof(ReportPacket) == kPacketSize, "packets must keep their size");

/// Floats travel as their bit patterns.
inline int32_t floatBits(float value) {
    int32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

inline float bitsToFloat(int32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline RequestPacket makeRequest(Request type, int32_t a = 0, int32_t b = 0, int32_t c = 0) {
    RequestPacket packet;
    packet.type = static_cast<uint8_t>(type);
    packet.a = a;
    packet.b = b;
    packet.c = c;
    return packet;
}

/// A request that carries a position on the display.
inline RequestPacket makeAimRequest(Request type, float x, float y) {
    return makeRequest(type, floatBits(x), floatBits(y));
}

inline ReportPacket makeStatus(State state, Reason reason = Reason::kNone) {
    ReportPacket packet;
    packet.type = static_cast<uint8_t>(Report::kStatus);
    packet.state = static_cast<uint8_t>(state);
    packet.reason = static_cast<uint16_t>(reason);
    return packet;
}

inline ReportPacket makeReady(int slotCount, int rawMaxX, int rawMaxY) {
    ReportPacket packet;
    packet.type = static_cast<uint8_t>(Report::kReady);
    packet.a = slotCount;
    packet.b = rawMaxX;
    packet.c = rawMaxY;
    return packet;
}

/// The secret that proves a client is the app, as two halves for the two packets that carry it.
struct Token {
    int32_t first = 0;
    int32_t second = 0;

    bool operator==(const Token& other) const { return first == other.first && second == other.second; }
};

}  // namespace aimbuddy::touch::protocol

#endif  // AIMBUDDY_CORE_TOUCH_PROTOCOL_H
