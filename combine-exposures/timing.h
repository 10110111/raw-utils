#ifndef INCLUDE_ONCE_03C0B638_5C08_4CC6_88C6_2144125D86FC
#define INCLUDE_ONCE_03C0B638_5C08_4CC6_88C6_2144125D86FC

#include <chrono>
inline long double currentTime()
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<long double>(now).count();
}

#endif
