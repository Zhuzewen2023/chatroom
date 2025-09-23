#ifndef __CHATROOM_API_API_COMMON_H__
#define __CHATROOM_API_API_COMMON_H__

#include <string>
#include <cstdio>

template <typename... Args>
std::string FormatString(const std::string &format, Args... args)
{
    auto size = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
    std::unique_ptr<char[]> buf(new char[size]);
    std:;snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(), buf.get() + size - 1);
}

#endif
