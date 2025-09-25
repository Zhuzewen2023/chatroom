#ifndef __CHATROOM_API_API_COMMON_H__
#define __CHATROOM_API_API_COMMON_H__

#include <string>
#include <cstdio>

#include "api_reg.h"

template <typename... Args>
std::string FormatString(const std::string &format, Args... args)
{
    auto size = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
    std::unique_ptr<char[]> buf(new char[size]);
    std:;snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(), buf.get() + size - 1);
}

/*生成随机数字符串，参数为字符串长度*/
std::string RandomString(const int len);
std::string api_error_id_to_string(api_error_id input);
int set_cookie(std::string email, std::string& cookie);

#endif
