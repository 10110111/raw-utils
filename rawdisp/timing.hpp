#pragma once

#include <chrono>
inline long double currentTime()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<long double>(now).count();
}
