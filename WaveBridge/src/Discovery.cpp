#include "Discovery.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>

namespace wb {
namespace {

std::string escapeJson(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (const char ch : input) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

std::optional<std::string> jsonString(const std::string& json, const std::string& key)
{
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        return match[1].str();
    }
    return std::nullopt;
}

std::optional<int> jsonInt(const std::string& json, const std::string& key)
{
    const std::regex pattern("\"" + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (std::regex_search(json, match, pattern)) {
        return std::stoi(match[1].str());
    }
    return std::nullopt;
}

bool jsonArrayContains(const std::string& json, const std::string& key, const std::string& value)
{
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) {
        return false;
    }
    return match[1].str().find("\"" + value + "\"") != std::string::npos;
}

std::vector<AudioCodec> parseCodecArray(const std::string& json)
{
    std::vector<AudioCodec> codecs;
    if (jsonArrayContains(json, "codecs", "pcm")) {
        codecs.push_back(AudioCodec::PcmS16);
    }
    if (jsonArrayContains(json, "codecs", "opus")) {
        codecs.push_back(AudioCodec::Opus);
    }
    return codecs;
}

std::string codecsJson(const std::vector<AudioCodec>& codecs)
{
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < codecs.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << "\"" << codecWireName(codecs[i]) << "\"";
    }
    out << "]";
    return out.str();
}

} // namespace

std::string makeDiscoveryRequestJson(const DiscoveryRequest& request)
{
    std::ostringstream out;
    out << "{\"type\":\"wavebridge.discover\","
        << "\"version\":1,"
        << "\"nonce\":\"" << escapeJson(request.nonce) << "\","
        << "\"maxPayload\":" << request.maxPayload << ","
        << "\"codecs\":" << codecsJson(request.codecs) << "}";
    return out.str();
}

std::optional<DiscoveryRequest> parseDiscoveryRequestJson(const std::string& json)
{
    const auto type = jsonString(json, "type");
    const auto version = jsonInt(json, "version");
    const auto nonce = jsonString(json, "nonce");
    const auto maxPayload = jsonInt(json, "maxPayload");

    if (!type || *type != "wavebridge.discover" || !version || *version != 1 || !nonce || !maxPayload) {
        return std::nullopt;
    }

    DiscoveryRequest request;
    request.nonce = *nonce;
    request.maxPayload = static_cast<std::size_t>(*maxPayload);
    request.codecs = parseCodecArray(json);
    return request;
}

std::string makePhoneReplyJson(
    const std::string& nonce,
    const std::string& deviceId,
    const std::string& name,
    std::uint16_t audioPort,
    std::size_t maxPayload,
    const std::vector<AudioCodec>& codecs)
{
    std::ostringstream out;
    out << "{\"type\":\"wavebridge.phone\","
        << "\"version\":1,"
        << "\"nonce\":\"" << escapeJson(nonce) << "\","
        << "\"deviceId\":\"" << escapeJson(deviceId) << "\","
        << "\"name\":\"" << escapeJson(name) << "\","
        << "\"audioPort\":" << audioPort << ","
        << "\"maxPayload\":" << maxPayload << ","
        << "\"codecs\":" << codecsJson(codecs) << "}";
    return out.str();
}

std::optional<PhoneInfo> parsePhoneReplyJson(
    const std::string& json,
    const std::string& expectedNonce,
    const sockaddr_in& from)
{
    const auto type = jsonString(json, "type");
    const auto version = jsonInt(json, "version");
    const auto nonce = jsonString(json, "nonce");
    const auto deviceId = jsonString(json, "deviceId");
    const auto name = jsonString(json, "name");
    const auto audioPort = jsonInt(json, "audioPort");
    const auto maxPayload = jsonInt(json, "maxPayload");

    if (!type || *type != "wavebridge.phone" || !version || *version != 1 || !nonce || *nonce != expectedNonce
        || !deviceId || !name || !audioPort || !maxPayload || *audioPort <= 0 || *audioPort > 65535) {
        return std::nullopt;
    }

    PhoneInfo phone;
    phone.deviceId = *deviceId;
    phone.name = *name;
    phone.audioPort = static_cast<std::uint16_t>(*audioPort);
    phone.maxPayload = static_cast<std::size_t>(*maxPayload);
    phone.codecs = parseCodecArray(json);
    phone.audioEndpoint = from;
    phone.audioEndpoint.sin_port = htons(phone.audioPort);

    char ip[INET_ADDRSTRLEN]{};
    InetNtopA(AF_INET, const_cast<IN_ADDR*>(&from.sin_addr), ip, static_cast<DWORD>(sizeof(ip)));
    phone.ip = ip;
    return phone;
}

bool supportsCodec(const PhoneInfo& phone, AudioCodec codec)
{
    return std::find(phone.codecs.begin(), phone.codecs.end(), codec) != phone.codecs.end();
}

std::vector<PhoneInfo> Discoverer::discover(
    std::uint16_t discoveryPort,
    std::size_t maxPayload,
    const std::vector<AudioCodec>& codecs,
    int timeoutMs,
    bool debug)
{
    SocketHandle socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket.valid()) {
        throw std::runtime_error("failed to create discovery socket: " + wsaError(WSAGetLastError()));
    }

    BOOL enabled = TRUE;
    setsockopt(socket.get(), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enabled), sizeof(enabled));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(0);
    if (bind(socket.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        throw std::runtime_error("failed to bind discovery socket: " + wsaError(WSAGetLastError()));
    }

    sockaddr_in broadcast{};
    broadcast.sin_family = AF_INET;
    broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    broadcast.sin_port = htons(discoveryPort);

    DiscoveryRequest request;
    request.nonce = randomHexNonce();
    request.maxPayload = maxPayload;
    request.codecs = codecs;
    const std::string payload = makeDiscoveryRequestJson(request);

    std::vector<PhoneInfo> phones;
    std::set<std::string> seen;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto nextSend = std::chrono::steady_clock::now();

    if (debug) {
        std::cout << "Discovery nonce " << request.nonce << " on UDP port " << discoveryPort << "\n";
    }

    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextSend) {
            const int sent = sendto(
                socket.get(),
                payload.data(),
                static_cast<int>(payload.size()),
                0,
                reinterpret_cast<const sockaddr*>(&broadcast),
                sizeof(broadcast));
            if (sent == SOCKET_ERROR && debug) {
                std::cerr << "Discovery broadcast failed: " << wsaError(WSAGetLastError()) << "\n";
            }
            nextSend = now + std::chrono::milliseconds(500);
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket.get(), &readSet);

        timeval wait{};
        wait.tv_sec = 0;
        wait.tv_usec = 100 * 1000;
        const int ready = select(0, &readSet, nullptr, nullptr, &wait);
        if (ready == SOCKET_ERROR) {
            throw std::runtime_error("discovery select failed: " + wsaError(WSAGetLastError()));
        }
        if (ready == 0 || !FD_ISSET(socket.get(), &readSet)) {
            continue;
        }

        char buffer[4096]{};
        sockaddr_in from{};
        int fromLength = sizeof(from);
        const int received = recvfrom(
            socket.get(),
            buffer,
            static_cast<int>(sizeof(buffer) - 1),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromLength);
        if (received == SOCKET_ERROR) {
            if (debug) {
                std::cerr << "Discovery receive failed: " << wsaError(WSAGetLastError()) << "\n";
            }
            continue;
        }

        const std::string json(buffer, buffer + received);
        auto phone = parsePhoneReplyJson(json, request.nonce, from);
        if (!phone) {
            if (debug) {
                std::cerr << "Ignored non-WaveBridge discovery reply from " << endpointToString(from) << "\n";
            }
            continue;
        }

        const std::string key = phone->deviceId + "@" + phone->ip;
        if (seen.insert(key).second) {
            if (debug) {
                std::cout << "Discovered " << phone->name << " at " << phone->ip
                    << " audio:" << phone->audioPort << "\n";
            }
            phones.push_back(*phone);
        }
    }

    return phones;
}

} // namespace wb
