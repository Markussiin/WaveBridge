#pragma once

#include "Common.h"
#include "PcmAudio.h"

#include <functional>

namespace wb {

class WasapiLoopbackCapture {
public:
    using FormatCallback = std::function<void(const AudioFormat&)>;
    using FramesCallback = std::function<void(const std::uint8_t* data, std::uint32_t frames, bool silent)>;

    void run(
        std::atomic_bool& stop,
        const FormatCallback& onFormat,
        const FramesCallback& onFrames,
        bool debug);
};

} // namespace wb
