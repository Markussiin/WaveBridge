# WaveBridge

WaveBridge is an experimental Windows audio bridge that streams your PC's system audio to a phone on the same local network.

The project started as a small personal itch: sometimes I had working headphones connected to my phone, but nothing useful for my PC. Rather than move hardware around or buy another adapter, I wanted a lightweight way to hear PC audio through the phone I already had next to me. WaveBridge is that idea turned into a C++ project.

This is a for-fun project, but it is being built like a real tool: clear protocol boundaries, testable sender behavior, and a simple path between the PC sender and phone receiver.

The matching Android receiver lives here:

https://github.com/Markussiin/WaveBridge-Android

## Status

WaveBridge currently implements the PC-side sender:

- UDP discovery for finding WaveBridge receivers on the same LAN.
- WASAPI loopback capture for default Windows system audio.
- PCM normalization to 48 kHz stereo S16LE.
- Low-latency frame splitting and UDP packetization.
- Optional Opus encoding through runtime-loaded `opus.dll` / `libopus.dll`.
- A local `mock-phone` mode for testing discovery and packet flow without a phone.
- Built-in `self-test` coverage for CLI parsing, discovery JSON, packet serialization, frame splitting, and PCM conversion.

The Android phone-side receiver is implemented separately in [WaveBridge-Android](https://github.com/Markussiin/WaveBridge-Android). PCM streaming works end to end; Opus support on Android is still planned.

## How It Works

```text
Windows system audio
        |
        v
WASAPI loopback capture
        |
        v
48 kHz stereo S16LE normalization
        |
        v
5 ms audio frame splitter
        |
        +--> PCM UDP payloads
        |
        +--> optional Opus UDP payloads
        |
        v
Selected phone receiver on the LAN
```

Discovery is PC initiated. The sender broadcasts a small JSON discovery request, receivers reply with their device metadata and supported codecs, and the PC streams to the selected receiver.

## Requirements

- Windows 10 or newer.
- Visual Studio 2022 Build Tools or newer.
- MSVC `v143` C++ toolset.
- Windows SDK.
- Optional: vcpkg `opus` package for Opus DLL deployment.

The default PCM sender path does not link to Opus directly. Opus is loaded at runtime only when `--codec opus` is selected.

## Build

Open `WaveBridge.slnx` in Visual Studio and build `x64`, or build from a Developer PowerShell:

```powershell
msbuild .\WaveBridge.slnx /p:Configuration=Release /p:Platform=x64
```

Run the built-in tests:

```powershell
.\x64\Release\WaveBridge.exe self-test
```

## Usage

For real phone playback, install and start the Android receiver:

https://github.com/Markussiin/WaveBridge-Android

Then start the Windows sender:

```powershell
.\x64\Release\WaveBridge.exe send --codec pcm --debug
```

For PC-only validation, start a mock receiver in one terminal:

```powershell
.\x64\Release\WaveBridge.exe mock-phone --debug
```

Start the sender in another terminal:

```powershell
.\x64\Release\WaveBridge.exe send --codec pcm --debug
```

Opus mode:

```powershell
.\x64\Release\WaveBridge.exe send --codec opus --debug
```

Common options:

```text
--codec pcm|opus
--frame-ms 5
--discover-port 37020
--audio-port 37021
--max-payload 1200
--opus-bitrate 96000
--debug
```

## Wire Protocol

Discovery uses UTF-8 JSON over UDP port `37020`.

Audio uses a manually serialized binary UDP packet header. The current magic is `PSNK` and the header includes:

- packet type: audio, start, stop, ping, pong
- codec: PCM S16 or Opus
- stream id
- packet sequence
- sample index
- sample rate, channel count, and frame size
- chunk index/count for payloads split across UDP datagrams

The default network audio format is 48 kHz stereo S16LE. The default frame size is 5 ms, and packet payloads are kept near MTU size by default.

## Project Layout

```text
WaveBridge/
  src/
    AudioCapture.*   WASAPI loopback capture
    Cli.*            command-line parsing
    Discovery.*      UDP discovery protocol
    MockPhone.*      local receiver for validation
    OpusEncoder.*    runtime Opus loading and encoding
    PcmAudio.*       PCM conversion and frame splitting
    Protocol.*       manual binary packet serialization
    Sender.*         PC sender pipeline
    SelfTest.*       built-in test runner
```

## Roadmap

- Add Android Opus decoding.
- Improve receiver-side jitter buffering and clock drift handling.
- Add device picker support for non-default Windows output devices.
- Add start/stop/ping/pong control packets to the runtime flow.
- Add CI once the project layout settles.
- Consider simple pairing for shared networks.

## Contributing

This project is early and intentionally small. Issues and experiments are welcome, especially around audio latency, packet loss behavior, Windows capture edge cases, and receiver design.

## License

Licensed under the [Apache License 2.0](LICENSE).

Redistributions and derivative works must retain the attribution notice in
[NOTICE](NOTICE), which credits Markuss Kruze as the original creator.
