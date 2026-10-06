#pragma once

#include <fmt/core.h>

#include <utility>

// Host threads (the watchdog, the GPU pump, guest-kernel logging) print from
// threads that must not throw. fmt::println throws when stdout stops accepting
// writes, and an uncaught throw in a std::thread calls std::terminate, which
// aborts the whole process. A lost log line is harmless; an abort is not.
template <typename... Args>
inline void HostPrintln(fmt::format_string<Args...> format, Args&&... args)
{
    try
    {
        fmt::println(format, std::forward<Args>(args)...);
    }
    catch (...)
    {
    }
}
