#pragma once

#include "Common.h"
#include "Protocol.h"

#include <optional>
#include <string>
#include <vector>

namespace wb {

struct DiscoveryRequest {
    std::string nonce;
    std::size_t maxPayload = 1200;
    std::vector<AudioCodec> codecs;
};

struct PhoneInfo {
    std::string deviceId;
    std::string name;
    std::string ip;
    std::uint16_t audioPort = 0;
    std::size_t maxPayload = 1200;
    std::vector<AudioCodec> codecs;
    sockaddr_in audioEndpoint{};
};

std::string makeDiscoveryRequestJson(const DiscoveryRequest& request);
std::optional<DiscoveryRequest> parseDiscoveryRequestJson(const std::string& json);

std::string makePhoneReplyJson(
    const std::string& nonce,
    const std::string& deviceId,
    const std::string& name,
    std::uint16_t audioPort,
    std::size_t maxPayload,
    const std::vector<AudioCodec>& codecs);

std::optional<PhoneInfo> parsePhoneReplyJson(
    const std::string& json,
    const std::string& expectedNonce,
    const sockaddr_in& from);

bool supportsCodec(const PhoneInfo& phone, AudioCodec codec);

class Discoverer {
public:
    std::vector<PhoneInfo> discover(
        std::uint16_t discoveryPort,
        std::size_t maxPayload,
        const std::vector<AudioCodec>& codecs,
        int timeoutMs,
        bool debug);
};

} // namespace wb
