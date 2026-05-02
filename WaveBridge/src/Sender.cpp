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
#include <sstream>

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
    auto lastStats = std::chrono::steady_clock::now();

    std::cout << "Streaming " << codecWireName(config.codec) << " to " << phone.name << " at "
        << endpointToString(phone.audioEndpoint) << ", frame " << config.frameMs
        << " ms, max UDP payload " << maxDatagramBytes << ". Press Ctrl+C to stop.\n";

    WasapiLoopbackCapture capture;
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

                for (const auto& packet : packets) {
                    const int sent = sendto(
                        audioSocket.get(),
                        reinterpret_cast<const char*>(packet.data()),
                        static_cast<int>(packet.size()),
                        0,
                        reinterpret_cast<const sockaddr*>(&phone.audioEndpoint),
                        sizeof(phone.audioEndpoint));
                    if (sent == SOCKET_ERROR) {
                        ++sendErrors;
                        if (config.debug) {
                            std::cerr << "Audio send failed: " << wsaError(WSAGetLastError()) << "\n";
                        }
                        continue;
                    }
                    ++packetsSent;
                    bytesSent += static_cast<std::uint64_t>(sent);
                }

                ++framesSent;
                nextSampleIndex += static_cast<std::uint64_t>(frameSamples);
            }

            const auto now = std::chrono::steady_clock::now();
            if (config.debug && now - lastStats >= std::chrono::seconds(1)) {
                std::cout << "sent frames=" << framesSent << " packets=" << packetsSent
                    << " bytes=" << bytesSent << " errors=" << sendErrors << "\n";
                lastStats = now;
            }
        },
        config.debug);

    std::cout << "Stopped. Sent frames=" << framesSent << ", packets=" << packetsSent
        << ", bytes=" << bytesSent << ", errors=" << sendErrors << "\n";
    return 0;
}

} // namespace wb
