#include "Cli.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace wb {
namespace {

bool isFlag(const std::string& value)
{
    return value.rfind("--", 0) == 0;
}

bool parseInt(const std::string& text, int minValue, int maxValue, int& out)
{
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value < minValue || value > maxValue) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

bool parseSize(const std::string& text, std::size_t minValue, std::size_t maxValue, std::size_t& out)
{
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value < minValue || value > maxValue) {
        return false;
    }
    out = static_cast<std::size_t>(value);
    return true;
}

bool validOpusFrameMs(int frameMs)
{
    return frameMs == 5 || frameMs == 10 || frameMs == 20 || frameMs == 40 || frameMs == 60;
}

} // namespace

ParseResult parseArgs(const std::vector<std::string>& args)
{
    ParseResult result;
    result.ok = true;

    std::size_t index = 1;
    if (index < args.size() && !isFlag(args[index])) {
        const std::string& mode = args[index++];
        if (mode == "send") {
            result.config.mode = AppMode::Send;
        } else if (mode == "mock-phone") {
            result.config.mode = AppMode::MockPhone;
        } else if (mode == "self-test") {
            result.config.mode = AppMode::SelfTest;
        } else if (mode == "help" || mode == "-h" || mode == "--help") {
            result.config.mode = AppMode::Help;
            result.showHelp = true;
        } else {
            result.ok = false;
            result.error = "unknown mode: " + mode;
            return result;
        }
    }

    while (index < args.size()) {
        const std::string flag = args[index++];
        auto requireValue = [&]() -> std::string {
            if (index >= args.size() || isFlag(args[index])) {
                throw std::runtime_error("missing value for " + flag);
            }
            return args[index++];
        };

        try {
            if (flag == "--help" || flag == "-h") {
                result.showHelp = true;
                result.config.mode = AppMode::Help;
            } else if (flag == "--debug") {
                result.config.debug = true;
            } else if (flag == "--codec") {
                AudioCodec codec{};
                const std::string value = requireValue();
                if (!parseCodecName(value, codec)) {
                    result.ok = false;
                    result.error = "unsupported codec: " + value;
                    return result;
                }
                result.config.codec = codec;
            } else if (flag == "--frame-ms") {
                int value = 0;
                if (!parseInt(requireValue(), 1, 60, value)) {
                    result.ok = false;
                    result.error = "--frame-ms must be between 1 and 60";
                    return result;
                }
                result.config.frameMs = value;
            } else if (flag == "--discover-port") {
                int value = 0;
                if (!parseInt(requireValue(), 1, 65535, value)) {
                    result.ok = false;
                    result.error = "--discover-port must be between 1 and 65535";
                    return result;
                }
                result.config.discoverPort = static_cast<std::uint16_t>(value);
            } else if (flag == "--audio-port") {
                int value = 0;
                if (!parseInt(requireValue(), 0, 65535, value)) {
                    result.ok = false;
                    result.error = "--audio-port must be between 0 and 65535";
                    return result;
                }
                result.config.audioPort = static_cast<std::uint16_t>(value);
            } else if (flag == "--max-payload") {
                std::size_t value = 0;
                if (!parseSize(requireValue(), 128, 65507, value)) {
                    result.ok = false;
                    result.error = "--max-payload must be between 128 and 65507";
                    return result;
                }
                result.config.maxPayload = value;
            } else if (flag == "--opus-bitrate") {
                int value = 0;
                if (!parseInt(requireValue(), 6000, 510000, value)) {
                    result.ok = false;
                    result.error = "--opus-bitrate must be between 6000 and 510000";
                    return result;
                }
                result.config.opusBitrate = value;
            } else if (flag == "--discovery-timeout-ms") {
                int value = 0;
                if (!parseInt(requireValue(), 250, 30000, value)) {
                    result.ok = false;
                    result.error = "--discovery-timeout-ms must be between 250 and 30000";
                    return result;
                }
                result.config.discoveryTimeoutMs = value;
            } else {
                result.ok = false;
                result.error = "unknown flag: " + flag;
                return result;
            }
        } catch (const std::exception& ex) {
            result.ok = false;
            result.error = ex.what();
            return result;
        }
    }

    if (result.config.codec == AudioCodec::Opus && !validOpusFrameMs(result.config.frameMs)) {
        result.ok = false;
        result.error = "Opus supports --frame-ms values 5, 10, 20, 40, or 60";
        return result;
    }

    return result;
}

ParseResult parseArgs(int argc, char** argv)
{
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i] != nullptr ? argv[i] : "");
    }
    return parseArgs(args);
}

void printUsage(std::ostream& out, const std::string& exeName)
{
    out << "Usage:\n"
        << "  " << exeName << " send [--codec pcm|opus] [--frame-ms 5] [--discover-port 37020]\n"
        << "      [--max-payload 1200] [--opus-bitrate 96000] [--debug]\n"
        << "  " << exeName << " mock-phone [--discover-port 37020] [--audio-port 37021]\n"
        << "      [--max-payload 1200] [--debug]\n"
        << "  " << exeName << " self-test\n\n"
        << "Defaults to send mode when no mode is supplied.\n";
}

} // namespace wb
