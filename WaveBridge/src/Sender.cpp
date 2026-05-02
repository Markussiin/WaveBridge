#include "Sender.h"

#include "AudioCapture.h"
#include "Discovery.h"
#include "OpusEncoder.h"
#include "PcmAudio.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

namespace wb {
namespace {

bool promptForPhone(const std::vector<PhoneInfo>& phones, PhoneInfo& selected)
{
    if (phones.empty()) {
        return false;
    }
    if (phones.size() == 1) {
        selected = phones.front();
        std::cout << "Auto-selected " << selected.name << " at " << selected.ip << "\n";
        return true;
    }

    std::cout << "Discovered phones:\n";
    for (std::size_t i = 0; i < phones.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << phones[i].name << " (" << phones[i].ip
            << ", audio " << phones[i].audioPort << ")\n";
    }

    while (true) {
        std::cout << "Select phone [1-" << phones.size() << "]: ";
        std::string line;
        if (!std::getline(std::cin, line)) {
            return false;
        }

        char* end = nullptr;
        const long value = std::strtol(line.c_str(), &end, 10);
        if (end != line.c_str() && *end == '\0' && value >= 1 && value <= static_cast<long>(phones.size())) {
            selected = phones[static_cast<std::size_t>(value - 1)];
            return true;
        }
        std::cout << "Invalid selection.\n";
    }
}

std::string codecList(const std::vector<AudioCodec>& codecs)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < codecs.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << codecWireName(codecs[i]);
    }
    return out.str();
}

bool sendPacket(SocketHandle& socket, const sockaddr_in& endpoint, const Bytes& packet, std::uint64_t& bytesSent, std::uint64_t& sendErrors, bool debug)
{
    const int sent = sendto(
        socket.get(),
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&endpoint),
        sizeof(endpoint));
    if (sent == SOCKET_ERROR) {
        ++sendErrors;
        if (debug) {
            std::cerr << "UDP send failed: " << wsaError(WSAGetLastError()) << "\n";
        }
        return false;
    }

    bytesSent += static_cast<std::uint64_t>(sent);
    return true;
}

} // namespace

int runSender(const AppConfig& config)
{
    WinsockInit winsock;

    std::vector<AudioCodec> localCodecs{AudioCodec::PcmS16, AudioCodec::Opus};
    Discoverer discoverer;

    std::cout << "Searching for WaveBridge phones on UDP port " << config.discoverPort << "...\n";
    auto phones = discoverer.discover(
        config.discoverPort,
        config.maxPayload,
        localCodecs,
        config.discoveryTimeoutMs,
        config.debug);

    if (phones.empty()) {
        std::cerr << "No phones discovered. Start the phone app or run `WaveBridge.exe mock-phone` on this network.\n";
        return 2;
    }

    PhoneInfo phone;
    if (!promptForPhone(phones, phone)) {
        std::cerr << "No phone selected.\n";
        return 2;
    }

    if (!supportsCodec(phone, config.codec)) {
        std::cerr << "Selected phone does not support codec " << codecWireName(config.codec)
            << ". Phone codecs: " << codecList(phone.codecs) << "\n";
        return 2;
    }

    const std::size_t maxDatagramBytes = std::min(config.maxPayload, phone.maxPayload);
    if (maxDatagramBytes <= kAudioPacketHeaderSize) {
        std::cerr << "Effective max payload is too small: " << maxDatagramBytes << "\n";
        return 2;
    }

    SocketHandle audioSocket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!audioSocket.valid()) {
        throw std::runtime_error("failed to create audio UDP socket: " + wsaError(WSAGetLastError()));
    }

    ConsoleStopSignal stop;
    const std::uint32_t streamId = randomU32();
    const int frameSamples = (static_cast<int>(kNetworkSampleRate) * config.frameMs) / 1000;

    std::unique_ptr<PcmConverter> converter;
    std::unique_ptr<FrameSplitter> splitter;
    std::unique_ptr<RuntimeOpusEncoder> opus;

    std::uint64_t nextSequence = 0;
    std::uint64_t nextSampleIndex = 0;
    std::uint64_t packetsSent = 0;
    std::uint64_t framesSent = 0;
    std::uint64_t bytesSent = 0;
    std::uint64_t sendErrors = 0;
    std::uint64_t controlsSent = 0;
    std::uint64_t pongsReceived = 0;
    std::uint64_t lastPongSequence = 0;
    std::uint64_t controlSequence = 0;
    std::mutex socketMutex;
    auto lastStats = std::chrono::steady_clock::now();

    std::cout << "Streaming " << codecWireName(config.codec) << " to " << phone.name << " at "
        << endpointToString(phone.audioEndpoint) << ", frame " << config.frameMs
        << " ms, max UDP payload " << maxDatagramBytes << ". Press Ctrl+C to stop.\n";

    {
        std::lock_guard<std::mutex> lock(socketMutex);
        const auto startPacket = makeControlPacket(PacketType::Start, config.codec, streamId, controlSequence++, static_cast<std::uint16_t>(frameSamples));
        if (sendPacket(audioSocket, phone.audioEndpoint, startPacket, bytesSent, sendErrors, config.debug)) {
            ++controlsSent;
        }
    }

    std::thread controlThread([&]() {
        auto nextPing = std::chrono::steady_clock::now();
        while (!stop.requested()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextPing) {
                std::lock_guard<std::mutex> lock(socketMutex);
                const auto ping = makeControlPacket(PacketType::Ping, config.codec, streamId, controlSequence++, static_cast<std::uint16_t>(frameSamples));
                if (sendPacket(audioSocket, phone.audioEndpoint, ping, bytesSent, sendErrors, config.debug)) {
                    ++controlsSent;
                }
                nextPing = now + std::chrono::seconds(1);
            }

            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(audioSocket.get(), &readSet);

            timeval wait{};
            wait.tv_sec = 0;
            wait.tv_usec = 200 * 1000;

            const int ready = select(0, &readSet, nullptr, nullptr, &wait);
            if (ready == SOCKET_ERROR) {
                if (config.debug) {
                    std::cerr << "Pong select failed: " << wsaError(WSAGetLastError()) << "\n";
                }
                continue;
            }

            if (ready > 0 && FD_ISSET(audioSocket.get(), &readSet)) {
                Bytes buffer(kAudioPacketHeaderSize + 16);
                sockaddr_in from{};
                int fromLength = sizeof(from);
                const int received = recvfrom(
                    audioSocket.get(),
                    reinterpret_cast<char*>(buffer.data()),
                    static_cast<int>(buffer.size()),
                    0,
                    reinterpret_cast<sockaddr*>(&from),
                    &fromLength);
                if (received <= 0) {
                    continue;
                }

                ParsedAudioPacket parsed;
                if (parseAudioPacket(buffer.data(), static_cast<std::size_t>(received), parsed)
                    && parsed.header.packetType == PacketType::Pong
                    && parsed.header.streamId == streamId) {
                    ++pongsReceived;
                    lastPongSequence = parsed.header.sequence;
                }
            }
        }
    });

    WasapiLoopbackCapture capture;
    try {
        capture.run(
            stop.flag(),
            [&](const AudioFormat& sourceFormat) {
                converter = std::make_unique<PcmConverter>(sourceFormat);
                splitter = std::make_unique<FrameSplitter>(kNetworkChannels, frameSamples);
                std::cout << "Capture: " << sourceFormat.description
                    << " -> " << kNetworkSampleRate << " Hz stereo s16le\n";

                if (config.codec == AudioCodec::Opus) {
                    opus = std::make_unique<RuntimeOpusEncoder>();
                    opus->open(kNetworkSampleRate, kNetworkChannels, config.opusBitrate);
                    std::cout << "Opus encoder loaded from " << opus->libraryPath()
                        << " at " << config.opusBitrate << " bps\n";
                }
            },
            [&](const std::uint8_t* data, std::uint32_t frames, bool silent) {
                if (!converter || !splitter) {
                    return;
                }

                auto normalized = converter->convert(data, frames, silent);
                auto audioFrames = splitter->append(normalized);

                for (const auto& frame : audioFrames) {
                    Bytes payload;
                    if (config.codec == AudioCodec::Opus) {
                        payload = opus->encode(frame.data(), frameSamples, maxDatagramBytes);
                    } else {
                        payload = pcmS16ToBytes(frame);
                    }

                    auto packets = packetizeFrame(
                        config.codec,
                        streamId,
                        nextSequence,
                        nextSampleIndex,
                        static_cast<std::uint16_t>(frameSamples),
                        payload,
                        maxDatagramBytes);

                    {
                        std::lock_guard<std::mutex> lock(socketMutex);
                        for (const auto& packet : packets) {
                            if (sendPacket(audioSocket, phone.audioEndpoint, packet, bytesSent, sendErrors, config.debug)) {
                                ++packetsSent;
                            }
                        }
                    }

                    ++framesSent;
                    nextSampleIndex += static_cast<std::uint64_t>(frameSamples);
                }

                const auto now = std::chrono::steady_clock::now();
                if (config.debug && now - lastStats >= std::chrono::seconds(1)) {
                    std::cout << "sent frames=" << framesSent << " packets=" << packetsSent
                        << " controls=" << controlsSent << " pongs=" << pongsReceived
                        << " lastPong=" << lastPongSequence
                        << " bytes=" << bytesSent << " errors=" << sendErrors << "\n";
                    lastStats = now;
                }
            },
            config.debug);
    } catch (...) {
        stop.flag().store(true);
        if (controlThread.joinable()) {
            controlThread.join();
        }
        throw;
    }

    stop.flag().store(true);
    if (controlThread.joinable()) {
        controlThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(socketMutex);
        const auto stopPacket = makeControlPacket(PacketType::Stop, config.codec, streamId, controlSequence++, static_cast<std::uint16_t>(frameSamples));
        if (sendPacket(audioSocket, phone.audioEndpoint, stopPacket, bytesSent, sendErrors, config.debug)) {
            ++controlsSent;
        }
    }

    std::cout << "Stopped. Sent frames=" << framesSent << ", packets=" << packetsSent
        << ", controls=" << controlsSent << ", pongs=" << pongsReceived
        << ", bytes=" << bytesSent << ", errors=" << sendErrors << "\n";
    return 0;
}

} // namespace wb
