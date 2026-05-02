#pragma once

#include "Protocol.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace wb {

enum class AppMode {
    Send,
    MockPhone,
    SelfTest,
    Help,
};

struct AppConfig {
    AppMode mode = AppMode::Send;
    AudioCodec codec = AudioCodec::PcmS16;
    int frameMs = 5;
    std::uint16_t discoverPort = 37020;
    std::uint16_t audioPort = 37021;
    std::size_t maxPayload = 1200;
    bool debug = false;
    int opusBitrate = 96000;
    int discoveryTimeoutMs = 1500;
};

struct ParseResult {
    bool ok = false;
    bool showHelp = false;
    std::string error;
    AppConfig config;
};

ParseResult parseArgs(const std::vector<std::string>& args);
ParseResult parseArgs(int argc, char** argv);
void printUsage(std::ostream& out, const std::string& exeName);

} // namespace wb
