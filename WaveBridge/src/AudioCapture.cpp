#include "AudioCapture.h"

#include <audioclient.h>
#include <avrt.h>
#include <iostream>
#include <memory>
#include <mmdeviceapi.h>
#include <wrl/client.h>

namespace wb {
namespace {

void throwHr(HRESULT hr, const char* operation)
{
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(operation) + " failed: " + hresultError(hr));
    }
}

struct WaveFormatDeleter {
    void operator()(WAVEFORMATEX* format) const
    {
        CoTaskMemFree(format);
    }
};

} // namespace

void WasapiLoopbackCapture::run(
    std::atomic_bool& stop,
    const FormatCallback& onFormat,
    const FramesCallback& onFrames,
    bool debug)
{
    ComInit com;

    using Microsoft::WRL::ComPtr;

    ComPtr<IMMDeviceEnumerator> enumerator;
    throwHr(CoCreateInstance(
                __uuidof(MMDeviceEnumerator),
                nullptr,
                CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator)),
        "CoCreateInstance(MMDeviceEnumerator)");

    ComPtr<IMMDevice> device;
    throwHr(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device), "GetDefaultAudioEndpoint");

    ComPtr<IAudioClient> audioClient;
    throwHr(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient), "Activate(IAudioClient)");

    WAVEFORMATEX* rawFormat = nullptr;
    throwHr(audioClient->GetMixFormat(&rawFormat), "GetMixFormat");
    std::unique_ptr<WAVEFORMATEX, WaveFormatDeleter> mixFormat(rawFormat);

    const AudioFormat sourceFormat = audioFormatFromWaveFormat(*mixFormat);
    onFormat(sourceFormat);

    if (debug) {
        std::cout << "WASAPI mix format: " << sourceFormat.description << "\n";
    }

    constexpr REFERENCE_TIME bufferDuration = 1'000'000; // 100 ms
    DWORD streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    HRESULT hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        bufferDuration,
        0,
        mixFormat.get(),
        nullptr);

    bool eventDriven = true;
    if (FAILED(hr)) {
        if (debug) {
            std::cerr << "Event-driven loopback initialize failed, falling back to polling: "
                << hresultError(hr) << "\n";
        }

        audioClient.Reset();
        throwHr(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient), "Activate(IAudioClient)");
        streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
        throwHr(audioClient->Initialize(
                    AUDCLNT_SHAREMODE_SHARED,
                    streamFlags,
                    bufferDuration,
                    0,
                    mixFormat.get(),
                    nullptr),
            "IAudioClient::Initialize");
        eventDriven = false;
    }

    HANDLE captureEvent = nullptr;
    if (eventDriven) {
        captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (captureEvent == nullptr) {
            throw std::runtime_error("CreateEvent failed: " + win32Error(GetLastError()));
        }
        throwHr(audioClient->SetEventHandle(captureEvent), "SetEventHandle");
    }

    ComPtr<IAudioCaptureClient> captureClient;
    throwHr(audioClient->GetService(IID_PPV_ARGS(&captureClient)), "GetService(IAudioCaptureClient)");

    DWORD taskIndex = 0;
    HANDLE avrtHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    throwHr(audioClient->Start(), "IAudioClient::Start");

    if (debug) {
        std::cout << "WASAPI loopback capture started. Press Ctrl+C to stop.\n";
    }

    try {
        while (!stop.load()) {
            if (eventDriven) {
                WaitForSingleObject(captureEvent, 200);
            } else {
                Sleep(5);
            }

            UINT32 packetFrames = 0;
            throwHr(captureClient->GetNextPacketSize(&packetFrames), "GetNextPacketSize");

            while (packetFrames > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                throwHr(captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr), "GetBuffer");

                const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                onFrames(reinterpret_cast<const std::uint8_t*>(data), frames, silent);

                throwHr(captureClient->ReleaseBuffer(frames), "ReleaseBuffer");
                throwHr(captureClient->GetNextPacketSize(&packetFrames), "GetNextPacketSize");
            }
        }

        audioClient->Stop();
    } catch (...) {
        audioClient->Stop();
        if (avrtHandle != nullptr) {
            AvRevertMmThreadCharacteristics(avrtHandle);
        }
        if (captureEvent != nullptr) {
            CloseHandle(captureEvent);
        }
        throw;
    }

    if (avrtHandle != nullptr) {
        AvRevertMmThreadCharacteristics(avrtHandle);
    }
    if (captureEvent != nullptr) {
        CloseHandle(captureEvent);
    }
}

} // namespace wb
