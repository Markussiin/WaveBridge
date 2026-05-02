#include "MockPhone.h"

#include "Discovery.h"

#include <chrono>
#include <iostream>
#include <sstream>

namespace wb {
namespace {

SocketHandle bindUdp(std::uint16_t port)
{
    SocketHandle socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket.valid()) {
        throw std::runtime_error("failed to create UDP socket: " + wsaError(WSAGetLastError()));
    }

    BOOL reuse = TRUE;
    setsockopt(socket.get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (bind(socket.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        throw std::runtime_error("failed to bind UDP port " + std::to_string(port) + ": " + wsaError(WSAGetLastError()));
    }

    return socket;
}

std::uint16_t boundPort(SOCKET socket)
{
    sockaddr_in address{};
    int length = sizeof(address);
    if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) == SOCKET_ERROR) {
        throw std::runtime_error("getsockname failed: " + wsaError(WSAGetLastError()));
    }
    return ntohs(address.sin_port);
}

std::string hostName()
{
    char name[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD size = static_cast<DWORD>(sizeof(name));
    if (GetComputerNameA(name, &size)) {
        return std::string(name, name + size);
    }
    return "pc";
}

} // namespace

int runMockPhone(const AppConfig& config)
{
    WinsockInit winsock;
    ConsoleStopSignal stop;

    auto discoverySocket = bindUdp(config.discoverPort);
    auto audioSocket = bindUdp(config.audioPort);
    const std::uint16_t audioPort = boundPort(audioSocket.get());

    const std::string host = hostName();
    const std::string deviceId = "mock-phone-" + host;
    const std::string displayName = "WaveBridge Mock Phone (" + host + ")";
    const std::vector<AudioCodec> codecs{AudioCodec::PcmS16, AudioCodec::Opus};

    std::cout << "Mock phone listening for discovery on UDP " << config.discoverPort
        << " and audio on UDP " << audioPort << ". Press Ctrl+C to stop.\n";

    std::uint64_t packetCount = 0;
    std::uint64_t byteCount = 0;
    std::uint64_t invalidCount = 0;
    std::uint64_t expectedSequence = 0;
    bool haveSequence = false;
    auto lastStats = std::chrono::steady_clock::now();

    while (!stop.requested()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(discoverySocket.get(), &readSet);
        FD_SET(audioSocket.get(), &readSet);

        timeval wait{};
        wait.tv_sec = 0;
        wait.tv_usec = 200 * 1000;

        const int ready = select(0, &readSet, nullptr, nullptr, &wait);
        if (ready == SOCKET_ERROR) {
            throw std::runtime_error("mock-phone select failed: " + wsaError(WSAGetLastError()));
        }

        if (ready > 0 && FD_ISSET(discoverySocket.get(), &readSet)) {
            char buffer[4096]{};
            sockaddr_in from{};
            int fromLength = sizeof(from);
            const int received = recvfrom(
                discoverySocket.get(),
                buffer,
                static_cast<int>(sizeof(buffer)),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromLength);

            if (received > 0) {
                const std::string json(buffer, buffer + received);
                auto request = parseDiscoveryRequestJson(json);
                if (request) {
                    const std::string reply = makePhoneReplyJson(
                        request->nonce,
                        deviceId,
                        displayName,
                        audioPort,
                        config.maxPayload,
                        codecs);
                    sendto(
                        discoverySocket.get(),
                        reply.data(),
                        static_cast<int>(reply.size()),
                        0,
                        reinterpret_cast<const sockaddr*>(&from),
                        sizeof(from));
                    if (config.debug) {
                        std::cout << "Replied to discovery from " << endpointToString(from)
                            << " nonce " << request->nonce << "\n";
                    }
                }
            }
        }

        if (ready > 0 && FD_ISSET(audioSocket.get(), &readSet)) {
            Bytes buffer(config.maxPayload + 512);
            sockaddr_in from{};
            int fromLength = sizeof(from);
            const int received = recvfrom(
                audioSocket.get(),
                reinterpret_cast<char*>(buffer.data()),
                static_cast<int>(buffer.size()),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromLength);

            if (received > 0) {
                ParsedAudioPacket packet;
                if (!parseAudioPacket(buffer.data(), static_cast<std::size_t>(received), packet)) {
                    ++invalidCount;
                    if (config.debug) {
                        std::cerr << "Invalid audio packet from " << endpointToString(from) << "\n";
                    }
                } else {
                    ++packetCount;
                    byteCount += static_cast<std::uint64_t>(received);

                    if (haveSequence && packet.header.sequence != expectedSequence) {
                        std::cerr << "Sequence jump: expected " << expectedSequence
                            << ", got " << packet.header.sequence << "\n";
                    }
                    expectedSequence = packet.header.sequence + 1;
                    haveSequence = true;

                    if (static_cast<std::size_t>(received) > config.maxPayload) {
                        std::cerr << "Oversized packet: " << received << " bytes\n";
                    }

                    if (config.debug && packetCount <= 5) {
                        std::cout << "Audio packet codec=" << codecWireName(packet.header.codec)
                            << " seq=" << packet.header.sequence
                            << " sample=" << packet.header.sampleIndex
                            << " chunk=" << packet.header.chunkIndex + 1 << "/" << packet.header.chunkCount
                            << " payload=" << packet.payloadLength << "\n";
                    }
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (config.debug && now - lastStats >= std::chrono::seconds(1)) {
            std::cout << "received packets=" << packetCount << " bytes=" << byteCount
                << " invalid=" << invalidCount << "\n";
            lastStats = now;
        }
    }

    std::cout << "Mock phone stopped. Received packets=" << packetCount
        << ", bytes=" << byteCount << ", invalid=" << invalidCount << "\n";
    return 0;
}

} // namespace wb
