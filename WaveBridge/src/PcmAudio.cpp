#include "PcmAudio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ks.h>
#include <ksmedia.h>
#include <sstream>

namespace wb {
namespace {

bool isGuidEqual(const GUID& a, const GUID& b)
{
    return IsEqualGUID(a, b) != 0;
}

int signed24(const std::uint8_t* data)
{
    int value = static_cast<int>(data[0]) | (static_cast<int>(data[1]) << 8) | (static_cast<int>(data[2]) << 16);
    if ((value & 0x800000) != 0) {
        value |= ~0xFFFFFF;
    }
    return value;
}

} // namespace

AudioFormat audioFormatFromWaveFormat(const WAVEFORMATEX& format)
{
    AudioFormat out;
    out.sampleRate = static_cast<int>(format.nSamplesPerSec);
    out.channels = static_cast<int>(format.nChannels);
    out.bitsPerSample = static_cast<int>(format.wBitsPerSample);
    out.validBitsPerSample = out.bitsPerSample;
    out.blockAlign = static_cast<int>(format.nBlockAlign);

    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        out.floatingPoint = true;
    } else if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        out.validBitsPerSample = static_cast<int>(extensible.Samples.wValidBitsPerSample);
        out.floatingPoint = isGuidEqual(extensible.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    out.description = describeAudioFormat(out);
    return out;
}

std::string describeAudioFormat(const AudioFormat& format)
{
    std::ostringstream out;
    out << format.sampleRate << " Hz, " << format.channels << " ch, "
        << format.bitsPerSample << "-bit " << (format.floatingPoint ? "float" : "PCM");
    return out.str();
}

PcmConverter::PcmConverter(AudioFormat source)
    : source_(std::move(source))
{
    if (source_.sampleRate <= 0 || source_.channels <= 0 || source_.blockAlign <= 0) {
        throw std::runtime_error("invalid source audio format");
    }
    sourceStep_ = static_cast<double>(source_.sampleRate) / static_cast<double>(kNetworkSampleRate);
}

std::vector<std::int16_t> PcmConverter::convert(const std::uint8_t* data, std::uint32_t frames, bool silent)
{
    std::vector<std::int16_t> output;
    if (frames == 0) {
        return output;
    }

    const std::size_t expectedFrames = static_cast<std::size_t>(
        std::ceil(static_cast<double>(frames) * static_cast<double>(kNetworkSampleRate) / source_.sampleRate)) + 2;
    output.reserve(expectedFrames * kNetworkChannels);

    while (phase_ < static_cast<double>(frames)) {
        const auto sourceFrame = static_cast<std::uint32_t>(phase_);
        const std::uint8_t* frame = nullptr;
        if (!silent && data != nullptr) {
            frame = data + static_cast<std::size_t>(sourceFrame) * source_.blockAlign;
        }

        const float left = frame != nullptr ? sampleAsFloat(frame, 0) : 0.0f;
        const float right = frame != nullptr ? sampleAsFloat(frame, source_.channels > 1 ? 1 : 0) : left;

        output.push_back(floatToS16(left));
        output.push_back(floatToS16(right));

        phase_ += sourceStep_;
    }

    phase_ -= static_cast<double>(frames);
    if (phase_ < 0.0) {
        phase_ = 0.0;
    }
    return output;
}

const AudioFormat& PcmConverter::sourceFormat() const
{
    return source_;
}

float PcmConverter::sampleAsFloat(const std::uint8_t* frame, int channel) const
{
    if (channel < 0 || channel >= source_.channels) {
        return 0.0f;
    }

    const int bytesPerSample = source_.bitsPerSample / 8;
    const std::uint8_t* sample = frame + channel * bytesPerSample;

    if (source_.floatingPoint) {
        if (source_.bitsPerSample == 32) {
            float value = 0.0f;
            std::memcpy(&value, sample, sizeof(value));
            return std::clamp(value, -1.0f, 1.0f);
        }
        if (source_.bitsPerSample == 64) {
            double value = 0.0;
            std::memcpy(&value, sample, sizeof(value));
            return static_cast<float>(std::clamp(value, -1.0, 1.0));
        }
        return 0.0f;
    }

    switch (source_.bitsPerSample) {
    case 8:
        return (static_cast<int>(*sample) - 128) / 128.0f;
    case 16: {
        std::int16_t value = 0;
        std::memcpy(&value, sample, sizeof(value));
        return value >= 0 ? value / 32767.0f : value / 32768.0f;
    }
    case 24:
    {
        const int value = signed24(sample);
        return value >= 0 ? value / 8388607.0f : value / 8388608.0f;
    }
    case 32: {
        std::int32_t value = 0;
        std::memcpy(&value, sample, sizeof(value));
        return value >= 0 ? value / 2147483647.0f : value / 2147483648.0f;
    }
    default:
        return 0.0f;
    }
}

std::int16_t PcmConverter::floatToS16(float sample) const
{
    const float clamped = std::clamp(sample, -1.0f, 1.0f);
    if (clamped >= 0.0f) {
        return static_cast<std::int16_t>(std::lround(clamped * 32767.0f));
    }
    return static_cast<std::int16_t>(std::lround(clamped * 32768.0f));
}

FrameSplitter::FrameSplitter(int channels, int frameSamplesPerChannel)
    : channels_(channels)
    , frameSamplesPerChannel_(frameSamplesPerChannel)
{
    if (channels_ <= 0 || frameSamplesPerChannel_ <= 0) {
        throw std::runtime_error("invalid frame splitter configuration");
    }
}

std::vector<std::vector<std::int16_t>> FrameSplitter::append(const std::vector<std::int16_t>& interleavedSamples)
{
    pending_.insert(pending_.end(), interleavedSamples.begin(), interleavedSamples.end());

    const std::size_t samplesPerFrame = static_cast<std::size_t>(channels_) * frameSamplesPerChannel_;
    std::vector<std::vector<std::int16_t>> frames;

    while (pending_.size() >= samplesPerFrame) {
        frames.emplace_back(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(samplesPerFrame));
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(samplesPerFrame));
    }

    return frames;
}

int FrameSplitter::frameSamplesPerChannel() const
{
    return frameSamplesPerChannel_;
}

Bytes pcmS16ToBytes(const std::vector<std::int16_t>& samples)
{
    Bytes bytes;
    bytes.reserve(samples.size() * sizeof(std::int16_t));
    for (const std::int16_t sample : samples) {
        const auto value = static_cast<std::uint16_t>(sample);
        bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    }
    return bytes;
}

} // namespace wb
