#ifndef AIMBUDDY_TOUCH_TOUCH_SERVER_H
#define AIMBUDDY_TOUCH_TOUCH_SERVER_H

#include <cstdint>
#include <memory>
#include <string>

#include "core/player_fingers.h"
#include "core/screen_mapping.h"
#include "core/touch_protocol.h"
#include "evdev_device.h"

namespace aimbuddy::touch {

/// How the helper was started.
struct ServerOptions {
    int port = 0;                       ///< TCP port on the loopback interface the app connects to; 0 lets the system pick one.
    protocol::Token token;              ///< Secret the app must present.
    std::string devicePath;             ///< Touchscreen to use; empty to pick the largest one.
    int clientWaitMs = 15000;           ///< How long to wait for the app to connect.
    int heartbeatMs = 700;              ///< Silence after which the touchscreen is given back.
    int deadClientMs = 5000;            ///< Silence after which the helper gives up and exits.
    int escapeHoldMs = 1500;            ///< How long both volume keys are held to take the screen back.
    bool verbose = false;
};

/// Why the helper stopped, as the process exit code.
enum ExitCode : int {
    kExitOk = 0,
    kExitUsage = 1,
    kExitSocket = 2,
    kExitNoClient = 3,
    kExitBadClient = 4,
    kExitNoTouchscreen = 5,
    kExitVirtualDevice = 6,
    kExitDeviceLost = 7,
    kExitEscapeKeys = 8,
    kExitClientGone = 9,
};

/**
 * The privileged half of touch assistance.
 *
 * It serves one client, the app, over a loopback connection. The app asks it to take over the
 * touchscreen; it then passes the player's touches on to a virtual copy of the screen (see
 * TouchMux) and adds the extra finger the app commands.
 *
 * Everything that could leave the player without a working screen is guarded:
 *  - The screen is only taken over when no finger is on it.
 *  - If the app stops sending heartbeats, the screen is given back at once.
 *  - Holding both volume keys gives the screen back and stops the helper.
 *  - If the helper is killed, the kernel gives the screen back by itself.
 *
 * The loop is single threaded: the touchscreen, the app's connection, the volume keys, signals
 * and a timer are all handled in turn, so no two things ever touch the same state.
 */
class TouchServer {
public:
    explicit TouchServer(const ServerOptions& options);
    ~TouchServer();

    /// Serves until told to stop. Returns an ExitCode.
    int run();

private:
    enum class Login { kAwaitingHello, kAwaitingToken, kDone };
    enum class Phase { kReleased, kWaitingForFingers, kMerging };

    struct DisplayConfig {
        int width = 0;
        int height = 0;
        int rotation = 0;
        bool isKnown() const { return width > 0 && height > 0; }
    };

    // Setup and teardown.
    bool openListener();
    bool openLoop();
    bool openDevices(protocol::Reason* reason);
    void closeEverything();
    void stop(int exitCode);

    // The loop.
    void dispatch(int fd, uint32_t events);
    void tick();
    int64_t nowNs() const;

    // The app's connection.
    void acceptClient();
    void readClient();
    void dropClient();
    bool handleRequest(const protocol::RequestPacket& packet);
    bool handleLogin(const protocol::RequestPacket& packet);
    void handleAim(const protocol::RequestPacket& packet);
    void send(const protocol::ReportPacket& packet);
    void sendStatus(protocol::State state, protocol::Reason reason = protocol::Reason::kNone);

    // The touchscreen.
    void readTouchscreen();
    void drainTouchscreen(TouchMux* target);
    void catchUp(TouchMux* target);
    void resetObserver();
    void readKeys(int fd);
    void checkVirtualDevice();
    void reportPlayerFingers(int64_t now);

    // Taking the screen over and giving it back.
    void engage();
    void grabNow();
    void disengage(protocol::Reason reason);

    ServerOptions options_;
    int exitCode_ = kExitOk;
    bool stopping_ = false;

    int listenFd_ = -1;
    int clientFd_ = -1;
    int epollFd_ = -1;
    int signalFd_ = -1;
    int timerFd_ = -1;

    Login login_ = Login::kAwaitingHello;
    uint8_t received_[protocol::kPacketSize] = {};
    int receivedBytes_ = 0;
    int64_t startedNs_ = 0;
    int64_t lastRequestNs_ = 0;
    int badClients_ = 0;

    DisplayConfig display_;
    Phase phase_ = Phase::kReleased;

    TouchscreenInfo screenInfo_;
    RawRange rawRange_;
    RealTouchscreen screen_;
    VirtualTouchscreen virtual_;
    std::unique_ptr<TouchMux> observer_;   ///< Follows the real screen's fingers while it is not taken over.
    std::unique_ptr<TouchMux> mux_;        ///< Merges the screen and the extra finger while it is.

    PlayerFingers player_;                 ///< How fast the player's own fingers move, for the app to hear of.
    bool playerWasDown_ = false;           ///< A finger was down at the last report, so one more is owed when the last lifts.

    std::vector<int> keyFds_;
    bool volumeUpHeld_ = false;
    bool volumeDownHeld_ = false;
    int64_t keysHeldSinceNs_ = 0;
};

}  // namespace aimbuddy::touch

#endif  // AIMBUDDY_TOUCH_TOUCH_SERVER_H
