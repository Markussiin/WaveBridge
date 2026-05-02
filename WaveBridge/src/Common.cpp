#include "Common.h"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace wb {
namespace {

std::atomic_bool* g_stopTarget = nullptr;

BOOL WINAPI consoleHandler(DWORD controlType)
{
    switch (controlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_stopTarget != nullptr) {
            g_stopTarget->store(true);
            return TRUE;
        }
        return FALSE;
    default:
        return FALSE;
    }
}

std::string formatSystemMessage(DWORD code, DWORD flags)
{
    LPSTR buffer = nullptr;
    const DWORD length = FormatMessageA(
        flags | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr);

    std::string message;
    if (length != 0 && buffer != nullptr) {
        message.assign(buffer, length);
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
            message.pop_back();
        }
        LocalFree(buffer);
    }

    if (message.empty()) {
        std::ostringstream out;
        out << "error " << code;
        message = out.str();
    }

    return message;
}

} // namespace

std::string win32Error(DWORD error)
{
    return formatSystemMessage(error, 0);
}

std::string hresultError(HRESULT hr)
{
    std::ostringstream out;
    out << "HRESULT 0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr)
        << ": " << formatSystemMessage(static_cast<DWORD>(hr), FORMAT_MESSAGE_FROM_HMODULE);
    return out.str();
}

std::string wsaError(int error)
{
    return "Winsock " + std::to_string(error) + ": " + win32Error(static_cast<DWORD>(error));
}

ComInit::ComInit()
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        initialized_ = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        initialized_ = false;
    } else {
        throw std::runtime_error("CoInitializeEx failed: " + hresultError(hr));
    }
}

ComInit::~ComInit()
{
    if (initialized_) {
        CoUninitialize();
    }
}

WinsockInit::WinsockInit()
{
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        throw std::runtime_error("WSAStartup failed: " + wsaError(result));
    }
}

WinsockInit::~WinsockInit()
{
    WSACleanup();
}

SocketHandle::SocketHandle(SOCKET socket)
    : socket_(socket)
{
}

SocketHandle::~SocketHandle()
{
    reset();
}

SocketHandle::SocketHandle(SocketHandle&& other) noexcept
    : socket_(other.release())
{
}

SocketHandle& SocketHandle::operator=(SocketHandle&& other) noexcept
{
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

SOCKET SocketHandle::get() const
{
    return socket_;
}

bool SocketHandle::valid() const
{
    return socket_ != INVALID_SOCKET;
}

SOCKET SocketHandle::release()
{
    const SOCKET socket = socket_;
    socket_ = INVALID_SOCKET;
    return socket;
}

void SocketHandle::reset(SOCKET socket)
{
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
    }
    socket_ = socket;
}

ConsoleStopSignal::ConsoleStopSignal()
{
    g_stopTarget = &stop_;
    SetConsoleCtrlHandler(consoleHandler, TRUE);
}

ConsoleStopSignal::~ConsoleStopSignal()
{
    SetConsoleCtrlHandler(consoleHandler, FALSE);
    if (g_stopTarget == &stop_) {
        g_stopTarget = nullptr;
    }
}

bool ConsoleStopSignal::requested() const
{
    return stop_.load();
}

std::atomic_bool& ConsoleStopSignal::flag()
{
    return stop_;
}

std::string endpointToString(const sockaddr_in& address)
{
    char host[INET_ADDRSTRLEN]{};
    InetNtopA(AF_INET, const_cast<IN_ADDR*>(&address.sin_addr), host, static_cast<DWORD>(sizeof(host)));

    std::ostringstream out;
    out << host << ":" << ntohs(address.sin_port);
    return out.str();
}

std::uint32_t randomU32()
{
    static thread_local std::mt19937 generator{
        static_cast<std::mt19937::result_type>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ GetCurrentProcessId())};
    return generator();
}

std::string randomHexNonce()
{
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8) << randomU32()
        << std::setw(8) << randomU32();
    return out.str();
}

} // namespace wb
