#include "Protocol.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace wb {
namespace {

void writeU16(Bytes& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void writeU32(Bytes& out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

void writeU64(Bytes& out, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

std::uint16_t readU16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

std::uint32_t readU32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint64_t readU64(const std::uint8_t* data)
{
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value <<= 8;
        value |= data[i];
    }
    return value;
}

} // namespace

std::string codecWireName(AudioCodec codec)
{
    switch (codec) {
    case AudioCodec::PcmS16:
        return "pcm";
    case AudioCodec::Opus:
        return "opus";
    default:
        return "unknown";
    }
}

bool parseCodecName(const std::string& name, AudioCodec& codec)
{
    if (name == "pcm" || name == "pcm_s16" || name == "pcm-s16") {
        codec = AudioCodec::PcmS16;
        return true;
    }
    if (name == "opus") {
        codec = AudioCodec::Opus;
        return true;
    }
    return false;
}

Bytes serializeAudioPacket(const AudioPacketHeader& header, const std::uint8_t* payload, std::size_t payloadLength)
{
    if (payloadLength > UINT16_MAX) {
        throw std::runtime_error("audio packet payload is too large");
    }
    if (header.chunkCount == 0 || header.chunkIndex >= header.chunkCount) {
        throw std::runtime_error("invalid audio packet chunk metadata");
    }

    Bytes out;
    out.reserve(kAudioPacketHeaderSize + payloadLength);
    out.push_back('P');
    out.push_back('S');
    out.push_back('N');
    out.push_back('K');
    writeU16(out, header.version);
    writeU16(out, static_cast<std::uint16_t>(kAudioPacketHeaderSize));
    out.push_back(static_cast<std::uint8_t>(header.packetType));
    out.push_back(static_cast<std::uint8_t>(header.codec));
    writeU16(out, header.flags);
    writeU32(out, header.streamId);
    writeU64(out, header.sequence);
    writeU64(out, header.sampleIndex);
    writeU32(out, header.sampleRate);
    writeU16(out, header.channels);
    writeU16(out, header.frameSamples);
    writeU16(out, header.chunkIndex);
    writeU16(out, header.chunkCount);
    writeU16(out, static_cast<std::uint16_t>(payloadLength));
    writeU16(out, header.reserved);

    if (out.size() != kAudioPacketHeaderSize) {
        throw std::runtime_error("audio packet header size mismatch");
    }

    if (payloadLength > 0) {
        out.insert(out.end(), payload, payload + payloadLength);
    }
    return out;
}

bool parseAudioPacket(const std::uint8_t* data, std::size_t size, ParsedAudioPacket& packet)
{
    if (data == nullptr || size < kAudioPacketHeaderSize) {
        return false;
    }
    if (data[0] != 'P' || data[1] != 'S' || data[2] != 'N' || data[3] != 'K') {
        return false;
    }
    if (readU16(data + 4) != kProtocolVersion || readU16(data + 6) != kAudioPacketHeaderSize) {
        return false;
    }

    AudioPacketHeader header;
    header.magic = kPacketMagic;
    header.version = readU16(data + 4);
    header.headerSize = readU16(data + 6);
    header.packetType = static_cast<PacketType>(data[8]);
    if (header.packetType != PacketType::Audio
        && header.packetType != PacketType::Start
        && header.packetType != PacketType::Stop
        && header.packetType != PacketType::Ping
        && header.packetType != PacketType::Pong) {
        return false;
    }
    header.codec = static_cast<AudioCodec>(data[9]);
    if (header.codec != AudioCodec::PcmS16 && header.codec != AudioCodec::Opus) {
        return false;
    }
    header.flags = readU16(data + 10);
    header.streamId = readU32(data + 12);
    header.sequence = readU64(data + 16);
    header.sampleIndex = readU64(data + 24);
    header.sampleRate = readU32(data + 32);
    header.channels = readU16(data + 36);
    header.frameSamples = readU16(data + 38);
    header.chunkIndex = readU16(data + 40);
    header.chunkCount = readU16(data + 42);
    header.payloadLength = readU16(data + 44);
    header.reserved = readU16(data + 46);

    if (header.chunkCount == 0 || header.chunkIndex >= header.chunkCount) {
        return false;
    }
    if (kAudioPacketHeaderSize + header.payloadLength > size) {
        return false;
    }

    packet.header = header;
    packet.payload = data + kAudioPacketHeaderSize;
    packet.payloadLength = header.payloadLength;
    return true;
}

Bytes makeControlPacket(PacketType type, AudioCodec codec, std::uint32_t streamId, std::uint64_t sequence, std::uint16_t frameSamples)
{
    if (type == PacketType::Audio) {
        throw std::runtime_error("control packet type cannot be Audio");
    }

    AudioPacketHeader header;
    header.packetType = type;
    header.codec = codec;
    header.streamId = streamId;
    header.sequence = sequence;
    header.sampleRate = kNetworkSampleRate;
    header.channels = kNetworkChannels;
    header.frameSamples = frameSamples;
    header.chunkIndex = 0;
    header.chunkCount = 1;
    header.payloadLength = 0;
    return serializeAudioPacket(header, nullptr, 0);
}

std::vector<Bytes> packetizeFrame(
    AudioCodec codec,
    std::uint32_t streamId,
    std::uint64_t& nextSequence,
    std::uint64_t sampleIndex,
    std::uint16_t frameSamples,
    const Bytes& payload,
    std::size_t maxDatagramBytes)
{
    if (maxDatagramBytes <= kAudioPacketHeaderSize) {
        throw std::runtime_error("max UDP payload must be larger than the WaveBridge header");
    }

    const std::size_t chunkPayloadLimit = maxDatagramBytes - kAudioPacketHeaderSize;
    const std::size_t chunkCountSize = std::max<std::size_t>(1, (payload.size() + chunkPayloadLimit - 1) / chunkPayloadLimit);
    if (chunkCountSize > UINT16_MAX) {
        throw std::runtime_error("audio frame requires too many UDP chunks");
    }

    std::vector<Bytes> packets;
    packets.reserve(chunkCountSize);

    for (std::size_t chunk = 0; chunk < chunkCountSize; ++chunk) {
        const std::size_t offset = chunk * chunkPayloadLimit;
        const std::size_t bytesLeft = offset < payload.size() ? payload.size() - offset : 0;
        const std::size_t thisPayload = std::min(chunkPayloadLimit, bytesLeft);

        AudioPacketHeader header;
        header.packetType = PacketType::Audio;
        header.codec = codec;
        header.streamId = streamId;
        header.sequence = nextSequence++;
        header.sampleIndex = sampleIndex;
        header.sampleRate = kNetworkSampleRate;
        header.channels = kNetworkChannels;
        header.frameSamples = frameSamples;
        header.chunkIndex = static_cast<std::uint16_t>(chunk);
        header.chunkCount = static_cast<std::uint16_t>(chunkCountSize);
        header.payloadLength = static_cast<std::uint16_t>(thisPayload);

        const std::uint8_t* chunkData = thisPayload > 0 ? payload.data() + offset : nullptr;
        packets.push_back(serializeAudioPacket(header, chunkData, thisPayload));
    }

    return packets;
}

} // namespace wb
