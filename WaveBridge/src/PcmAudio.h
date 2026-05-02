#pragma once

#include "Common.h"
#include "Protocol.h"

#include <mmreg.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wb {

struct AudioFormat {
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    int validBitsPerSample = 0;
    int blockAlign = 0;
    bool floatingPoint = false;
    std::string description;
};

AudioFormat audioFormatFromWaveFormat(const WAVEFORMATEX& format);
std::string describeAudioFormat(const AudioFormat& format);

class PcmConverter {
public:
    explicit PcmConverter(AudioFormat source);

    std::vector<std::int16_t> convert(const std::uint8_t* data, std::uint32_t frames, bool silent);
    const AudioFormat& sourceFormat() const;

private:
    float sampleAsFloat(const std::uint8_t* frame, int channel) const;
    std::int16_t floatToS16(float sample) const;

    AudioFormat source_;
    double phase_ = 0.0;
    double sourceStep_ = 1.0;
};

class FrameSplitter {
public:
    FrameSplitter(int channels, int frameSamplesPerChannel);

    std::vector<std::vector<std::int16_t>> append(const std::vector<std::int16_t>& interleavedSamples);
    int frameSamplesPerChannel() const;

private:
    int channels_ = 2;
    int frameSamplesPerChannel_ = 240;
    std::vector<std::int16_t> pending_;
};

Bytes pcmS16ToBytes(const std::vector<std::int16_t>& samples);

} // namespace wb
