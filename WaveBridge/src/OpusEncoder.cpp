#include "OpusEncoder.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace wb {
namespace {

constexpr int kOpusApplicationAudio = 2049;
constexpr int kOpusSetBitrateRequest = 4002;

std::string narrowModulePath(HMODULE module)
{
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0) {
        return "opus";
    }

    char output[MAX_PATH * 4]{};
    const int converted = WideCharToMultiByte(CP_UTF8, 0, path, static_cast<int>(length), output, sizeof(output), nullptr, nullptr);
    return converted > 0 ? std::string(output, output + converted) : "opus";
}

FARPROC requiredProc(HMODULE library, const char* name)
{
    FARPROC proc = GetProcAddress(library, name);
    if (proc == nullptr) {
        throw std::runtime_error(std::string("loaded Opus library is missing symbol ") + name);
    }
    return proc;
}

} // namespace

RuntimeOpusEncoder::~RuntimeOpusEncoder()
{
    if (encoder_ != nullptr && destroy_ != nullptr) {
        destroy_(encoder_);
        encoder_ = nullptr;
    }
    if (library_ != nullptr) {
        FreeLibrary(library_);
        library_ = nullptr;
    }
}

void RuntimeOpusEncoder::open(int sampleRate, int channels, int bitrate)
{
    if (encoder_ != nullptr) {
        return;
    }

    static constexpr std::array<const wchar_t*, 2> candidates{L"opus.dll", L"libopus.dll"};
    for (const wchar_t* candidate : candidates) {
        library_ = LoadLibraryW(candidate);
        if (library_ != nullptr) {
            break;
        }
    }
    if (library_ == nullptr) {
        throw std::runtime_error("Opus requested, but opus.dll/libopus.dll was not found on PATH. Install vcpkg opus or copy the DLL beside WaveBridge.exe.");
    }

    libraryPath_ = narrowModulePath(library_);
    create_ = reinterpret_cast<CreateFn>(requiredProc(library_, "opus_encoder_create"));
    destroy_ = reinterpret_cast<DestroyFn>(requiredProc(library_, "opus_encoder_destroy"));
    encode_ = reinterpret_cast<EncodeFn>(requiredProc(library_, "opus_encode"));
    ctl_ = reinterpret_cast<CtlFn>(requiredProc(library_, "opus_encoder_ctl"));

    int error = 0;
    encoder_ = create_(sampleRate, channels, kOpusApplicationAudio, &error);
    if (encoder_ == nullptr || error != 0) {
        throw std::runtime_error("opus_encoder_create failed with code " + std::to_string(error));
    }

    if (bitrate > 0) {
        ctl_(encoder_, kOpusSetBitrateRequest, bitrate);
    }
}

Bytes RuntimeOpusEncoder::encode(const std::int16_t* pcm, int frameSamplesPerChannel, std::size_t maxBytes)
{
    if (encoder_ == nullptr || encode_ == nullptr) {
        throw std::runtime_error("Opus encoder is not open");
    }
    if (maxBytes == 0 || maxBytes > 65507) {
        throw std::runtime_error("invalid Opus output size");
    }

    Bytes out(maxBytes);
    const int encoded = encode_(encoder_, pcm, frameSamplesPerChannel, out.data(), static_cast<std::int32_t>(out.size()));
    if (encoded < 0) {
        throw std::runtime_error("opus_encode failed with code " + std::to_string(encoded));
    }

    out.resize(static_cast<std::size_t>(encoded));
    return out;
}

bool RuntimeOpusEncoder::isOpen() const
{
    return encoder_ != nullptr;
}

const std::string& RuntimeOpusEncoder::libraryPath() const
{
    return libraryPath_;
}

} // namespace wb
