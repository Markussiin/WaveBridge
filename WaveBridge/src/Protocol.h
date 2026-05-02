#pragma once

#include "Common.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace wb {

enum class AudioCodec : std::uint8_t {
    PcmS16 = 0,
    Opus = 1,
};

enum class PacketType : std::uint8_t {
    Audio = 0,
    Start = 1,
    Stop = 2,
    Ping = 3,
    Pong = 4,
};

enum class AudioFlags : std::uint16_t {
    None = 0,
    EndOfStream = 1 << 0,
    Discontinuity = 1 << 1,
};

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::uint32_t kPacketMagic = 0x50534E4B; // "PSNK"
constexpr std::size_t kAudioPacketHeaderSize = 48;
constexpr std::uint32_t kNetworkSampleRate = 48000;
constexpr std::uint16_t kNetworkChannels = 2;

struct AudioPacketHeader {
    std::uint32_t magic = kPacketMagic;
    std::uint16_t version = kProtocolVersion;
    std::uint16_t headerSize = static_cast<std::uint16_t>(kAudioPacketHeaderSize);
    PacketType packetType = PacketType::Audio;
    AudioCodec codec = AudioCodec::PcmS16;
    std::uint16_t flags = 0;
    std::uint32_t streamId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t sampleIndex = 0;
    std::uint32_t sampleRate = kNetworkSampleRate;
    std::uint16_t channels = kNetworkChannels;
    std::uint16_t frameSamples = 0;
    std::uint16_t chunkIndex = 0;
    std::uint16_t chunkCount = 1;
    std::uint16_t payloadLength = 0;
    std::uint16_t reserved = 0;
};

struct ParsedAudioPacket {
    AudioPacketHeader header;
    const std::uint8_t* payload = nullptr;
    std::size_t payloadLength = 0;
};

std::string codecWireName(AudioCodec codec);
bool parseCodecName(const std::string& name, AudioCodec& codec);

Bytes serializeAudioPacket(const AudioPacketHeader& header, const std::uint8_t* payload, std::size_t payloadLength);
bool parseAudioPacket(const std::uint8_t* data, std::size_t size, ParsedAudioPacket& packet);
Bytes makeControlPacket(PacketType type, AudioCodec codec, std::uint32_t streamId, std::uint64_t sequence, std::uint16_t frameSamples);

std::vector<Bytes> packetizeFrame(
    AudioCodec codec,
    std::uint32_t streamId,
    std::uint64_t& nextSequence,
    std::uint64_t sampleIndex,
    std::uint16_t frameSamples,
    const Bytes& payload,
    std::size_t maxDatagramBytes);

} // namespace wb
