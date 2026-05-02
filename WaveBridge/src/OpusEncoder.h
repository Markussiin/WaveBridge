#pragma once

#include "Common.h"

#include <cstdint>
#include <string>

namespace wb {

class RuntimeOpusEncoder {
public:
    RuntimeOpusEncoder() = default;
    ~RuntimeOpusEncoder();

    RuntimeOpusEncoder(const RuntimeOpusEncoder&) = delete;
    RuntimeOpusEncoder& operator=(const RuntimeOpusEncoder&) = delete;

    void open(int sampleRate, int channels, int bitrate);
    Bytes encode(const std::int16_t* pcm, int frameSamplesPerChannel, std::size_t maxBytes);
    bool isOpen() const;
    const std::string& libraryPath() const;

private:
    struct OpusEncoderHandle;

    using CreateFn = OpusEncoderHandle* (__cdecl*)(int, int, int, int*);
    using DestroyFn = void (__cdecl*)(OpusEncoderHandle*);
    using EncodeFn = int (__cdecl*)(OpusEncoderHandle*, const std::int16_t*, int, unsigned char*, std::int32_t);
    using CtlFn = int (__cdecl*)(OpusEncoderHandle*, int, ...);

    HMODULE library_ = nullptr;
    OpusEncoderHandle* encoder_ = nullptr;
    CreateFn create_ = nullptr;
    DestroyFn destroy_ = nullptr;
    EncodeFn encode_ = nullptr;
    CtlFn ctl_ = nullptr;
    std::string libraryPath_;
};

} // namespace wb
