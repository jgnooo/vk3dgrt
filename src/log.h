#pragma once

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#endif


namespace Log {

// ─── ANSI escape codes ───────────────────────────────────────

namespace Color {
    constexpr const char* Reset      = "\033[0m";
    constexpr const char* Bold       = "\033[1m";
    constexpr const char* Dim        = "\033[2m";
    constexpr const char* Red        = "\033[31m";
    constexpr const char* Green      = "\033[32m";
    constexpr const char* Yellow     = "\033[33m";
    constexpr const char* Blue       = "\033[34m";
    constexpr const char* Magenta    = "\033[35m";
    constexpr const char* Cyan       = "\033[36m";
    constexpr const char* Gray       = "\033[90m";
    constexpr const char* BrightRed    = "\033[91m";
    constexpr const char* BrightGreen  = "\033[92m";
    constexpr const char* BrightYellow = "\033[93m";
    constexpr const char* BrightCyan   = "\033[96m";
    constexpr const char* White        = "\033[97m";
}


// ─── Platform init ───────────────────────────────────────────

inline void init()
{
#ifdef _WIN32
    auto enableVT = [](DWORD handleType)
    {
        HANDLE h = GetStdHandle(handleType);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD mode = 0;
        if (!GetConsoleMode(h, &mode)) return;
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    };
    enableVT(STD_OUTPUT_HANDLE);
    enableVT(STD_ERROR_HANDLE);
#endif
}


// ─── Format helpers ──────────────────────────────────────────

inline std::string formatMemory(size_t bytes)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    if (bytes >= 1024ULL * 1024 * 1024)
        ss << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    else if (bytes >= 1024ULL * 1024)
        ss << (bytes / (1024.0 * 1024.0)) << " MB";
    else if (bytes >= 1024)
        ss << (bytes / 1024.0) << " KB";
    else
        ss << bytes << " B";
    return ss.str();
}

inline std::string formatCount(size_t count)
{
    std::string num = std::to_string(count);
    std::string result;
    int pos = 0;
    for (int i = static_cast<int>(num.size()) - 1; i >= 0; --i, ++pos)
    {
        if (pos > 0 && pos % 3 == 0)
            result = "," + result;
        result = num[i] + result;
    }
    return result;
}


// ─── RAII log stream ─────────────────────────────────────────

class LogStream
{
public:
    LogStream(std::ostream& os,
              const char* prefix, const char* prefixColor,
              const char* tag)
        : os_(os), active_(true)
    {
        os_ << prefixColor << prefix << Color::Reset
            << Color::Dim << "[" << Color::Reset
            << Color::Bold << tag << Color::Reset
            << Color::Dim << "]" << Color::Reset << " ";
    }

    ~LogStream()
    {
        if (active_)
            os_ << std::endl;
    }

    LogStream(LogStream&& other) noexcept
        : os_(other.os_), active_(other.active_)
    {
        other.active_ = false;
    }

    template <typename T>
    LogStream& operator<<(const T& val)
    {
        os_ << val;
        return *this;
    }

    LogStream(const LogStream&)            = delete;
    LogStream& operator=(const LogStream&) = delete;

private:
    std::ostream& os_;
    bool          active_;
};


// ─── Log level functions ─────────────────────────────────────

inline LogStream INFO(const char* tag)
{
    return LogStream(std::cout, "  INFO ", Color::Cyan, tag);
}

inline LogStream OK(const char* tag)
{
    return LogStream(std::cout, "    OK ", Color::BrightGreen, tag);
}

inline LogStream WARN(const char* tag)
{
    return LogStream(std::cout, "  WARN ", Color::BrightYellow, tag);
}

inline LogStream ERR(const char* tag)
{
    return LogStream(std::cerr, " ERROR ", Color::BrightRed, tag);
}


// ─── Banner ──────────────────────────────────────────────────

inline void banner()
{
    std::cout << "\n"
              << Color::BrightCyan << Color::Bold
              << "  vk3dgrt" << Color::Reset
              << Color::Dim << "  " << Color::Reset
              << "Vulkan 3D Gaussian Ray Tracer"
              << "\n" << std::endl;
}

}   // namespace Log
