#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace wb {

using Bytes = std::vector<std::uint8_t>;

std::string win32Error(DWORD error);
std::string hresultError(HRESULT hr);
std::string wsaError(int error);

class ComInit {
public:
    ComInit();
    ~ComInit();

    ComInit(const ComInit&) = delete;
    ComInit& operator=(const ComInit&) = delete;

private:
    bool initialized_ = false;
};

class WinsockInit {
public:
    WinsockInit();
    ~WinsockInit();

    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;
};

class SocketHandle {
public:
    SocketHandle() = default;
    explicit SocketHandle(SOCKET socket);
    ~SocketHandle();

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept;
    SocketHandle& operator=(SocketHandle&& other) noexcept;

    SOCKET get() const;
    bool valid() const;
    SOCKET release();
    void reset(SOCKET socket = INVALID_SOCKET);

private:
    SOCKET socket_ = INVALID_SOCKET;
};

class ConsoleStopSignal {
public:
    ConsoleStopSignal();
    ~ConsoleStopSignal();

    ConsoleStopSignal(const ConsoleStopSignal&) = delete;
    ConsoleStopSignal& operator=(const ConsoleStopSignal&) = delete;

    bool requested() const;
    std::atomic_bool& flag();

private:
    std::atomic_bool stop_{false};
};

std::string endpointToString(const sockaddr_in& address);
std::uint32_t randomU32();
std::string randomHexNonce();

} // namespace wb
