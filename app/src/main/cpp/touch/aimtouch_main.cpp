// The touch helper: a small program started with the privileges to read the touchscreen and create
// virtual input devices (root, or the shell user through Shizuku). The app talks to it over a
// loopback connection. See touch_server.h for what it does and how it keeps the screen usable.

#include <sys/resource.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "touch_server.h"

namespace {

void printUsage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s --port <port, or 0 to let the system pick> --token <16 hex digits> [--device /dev/input/eventN] [--verbose]\n"
                 "          [--client-wait-ms N] [--heartbeat-ms N] [--escape-hold-ms N]\n",
                 program);
}

/// Reads 16 hexadecimal digits as the two halves of the secret token.
bool parseToken(const std::string& text, aimbuddy::touch::protocol::Token* token) {
    if (text.size() != 16) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 16);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    token->first = static_cast<int32_t>(static_cast<uint32_t>(value >> 32));
    token->second = static_cast<int32_t>(static_cast<uint32_t>(value & 0xFFFFFFFFu));
    return true;
}

/// Reads the command line into `options`. Returns false if it is not usable.
bool parseArguments(int argc, char** argv, aimbuddy::touch::ServerOptions* options) {
    bool hasPort = false;
    bool hasToken = false;
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];
        const bool hasValue = i + 1 < argc;
        if (name == "--verbose") {
            options->verbose = true;
        } else if (name == "--port" && hasValue) {
            options->port = std::atoi(argv[++i]);
            hasPort = options->port >= 0 && options->port < 65536;
        } else if (name == "--token" && hasValue) {
            hasToken = parseToken(argv[++i], &options->token);
        } else if (name == "--device" && hasValue) {
            options->devicePath = argv[++i];
        } else if (name == "--client-wait-ms" && hasValue) {
            options->clientWaitMs = std::atoi(argv[++i]);
        } else if (name == "--heartbeat-ms" && hasValue) {
            options->heartbeatMs = std::atoi(argv[++i]);
        } else if (name == "--escape-hold-ms" && hasValue) {
            options->escapeHoldMs = std::atoi(argv[++i]);
        } else {
            return false;
        }
    }
    return hasPort && hasToken;
}

}  // namespace

int main(int argc, char** argv) {
    aimbuddy::touch::ServerOptions options;
    if (!parseArguments(argc, argv, &options)) {
        printUsage(argv[0]);
        return aimbuddy::touch::kExitUsage;
    }
    // Forwarding touches is time critical. Only root may raise its priority; for anyone else this does nothing.
    setpriority(PRIO_PROCESS, 0, -8);

    aimbuddy::touch::TouchServer server(options);
    return server.run();
}
