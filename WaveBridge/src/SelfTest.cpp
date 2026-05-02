#include "SelfTest.h"

#include "Cli.h"
#include "Discovery.h"
#include "PcmAudio.h"

#include <iostream>
#include <stdexcept>

namespace wb {
namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testCli()
{
    auto defaults = parseArgs(std::vector<std::string>{"WaveBridge.exe"});
    expect(defaults.ok, "default CLI parse failed");
    expect(defaults.config.mode == AppMode::Send, "default mode should be send");
    expect(defaults.config.codec == AudioCodec::PcmS16, "default codec should be PCM");

    auto opus = parseArgs(std::vector<std::string>{"WaveBridge.exe", "send", "--codec", "opus", "--frame-ms", "10"});
    expect(opus.ok, "opus CLI parse failed");
    expect(opus.config.codec == AudioCodec::Opus, "opus codec not selected");

    auto bad = parseArgs(std::vector<std::string>{"WaveBridge.exe", "send", "--codec", "opus", "--frame-ms", "7"});
    expect(!bad.ok, "invalid Opus frame size should fail");
}

void testDiscoveryJson()
{
    DiscoveryRequest request;
    request.nonce = "abc123";
    request.maxPayload = 1200;
    request.codecs = {AudioCodec::PcmS16, AudioCodec::Opus};

    const auto requestJson = makeDiscoveryRequestJson(request);
    const auto parsedRequest = parseDiscoveryRequestJson(requestJson);
    expect(parsedRequest.has_value(), "discovery request parse failed");
    expect(parsedRequest->nonce == request.nonce, "discovery request nonce mismatch");
    expect(parsedRequest->codecs.size() == 2, "discovery request codecs mismatch");

    sockaddr_in from{};
    from.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &from.sin_addr);
    from.sin_port = htons(50000);

    const auto replyJson = makePhoneReplyJson("abc123", "phone1", "Phone One", 37021, 1200, request.codecs);
    const auto phone = parsePhoneReplyJson(replyJson, "abc123", from);
    expect(phone.has_value(), "phone reply parse failed");
    expect(phone->deviceId == "phone1", "phone device id mismatch");
    expect(phone->audioPort == 37021, "phone audio port mismatch");
    expect(supportsCodec(*phone, AudioCodec::Opus), "phone should support Opus");
}

void testProtocol()
{
    Bytes payload{1, 2, 3, 4, 5};
    AudioPacketHeader header;
    header.codec = AudioCodec::PcmS16;
    header.streamId = 42;
    header.sequence = 7;
    header.sampleIndex = 240;
    header.frameSamples = 240;
    header.payloadLength = static_cast<std::uint16_t>(payload.size());

    const auto bytes = serializeAudioPacket(header, payload.data(), payload.size());
    ParsedAudioPacket parsed;
    expect(parseAudioPacket(bytes.data(), bytes.size(), parsed), "audio packet parse failed");
    expect(parsed.header.streamId == 42, "stream id mismatch");
    expect(parsed.header.sequence == 7, "sequence mismatch");
    expect(parsed.payloadLength == payload.size(), "payload length mismatch");

    std::uint64_t nextSequence = 0;
    const auto packets = packetizeFrame(AudioCodec::PcmS16, 1, nextSequence, 0, 240, payload, kAudioPacketHeaderSize + 2);
    expect(packets.size() == 3, "packet chunk count mismatch");
    expect(nextSequence == 3, "packet sequence counter mismatch");
}

void testPcm()
{
    AudioFormat format;
    format.sampleRate = 48000;
    format.channels = 1;
    format.bitsPerSample = 16;
    format.validBitsPerSample = 16;
    format.blockAlign = 2;
    format.floatingPoint = false;

    const std::uint8_t source[] = {
        0x00, 0x00,
        0xff, 0x7f,
    };

    PcmConverter converter(format);
    const auto converted = converter.convert(source, 2, false);
    expect(converted.size() == 4, "PCM mono-to-stereo conversion size mismatch");
    expect(converted[0] == 0 && converted[1] == 0, "PCM zero conversion mismatch");
    expect(converted[2] == 32767 && converted[3] == 32767, "PCM positive conversion mismatch");

    FrameSplitter splitter(2, 2);
    const auto frames = splitter.append(converted);
    expect(frames.size() == 1, "frame splitter should emit one frame");
    expect(pcmS16ToBytes(frames[0]).size() == 8, "PCM byte conversion mismatch");
}

} // namespace

int runSelfTests()
{
    try {
        testCli();
        testDiscoveryJson();
        testProtocol();
        testPcm();
    } catch (const std::exception& ex) {
        std::cerr << "self-test failed: " << ex.what() << "\n";
        return 1;
    }

    std::cout << "self-test passed\n";
    return 0;
}

} // namespace wb
