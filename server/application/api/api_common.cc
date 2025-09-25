#include "api_common.h"
#include "api_reg.h"

/*生成随机数字符串，参数为字符串长度*/
std::string RandomString(const int len)
{
    std::string str;
    char c;
    int idx;
    for (idx = 0; idx < len; idx++) {
        c = 'a' + rand() % 26;
        str.push_back(c);
    }
    return str;
}

std::string api_error_id_to_string(api_error_id input)
{
    switch (input) {
        case api_error_id::login_failed: return "LOGIN_FAILED";
        case api_error_id::username_exists: return "USERNAME_EXISTS";
        case api_error_id::email_exists: return "EMAIL_EXISTS";
        case api_error_id::bad_request: return "";
        default: return "BAD_REQUEST";
    }
}

int set_cookie(std::string email, std::string& cookie)
{
    /*获取redis连接池*/
    
}