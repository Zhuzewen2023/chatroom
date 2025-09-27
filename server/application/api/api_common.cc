#include "api_common.h"
#include "api_reg.h"
#include "cache_pool.h"

#include <uuid/uuid.h>
#include <vector>

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

std::string generate_uuid()
{
    uuid_t uuid;
    uuid_generate_time_safe(uuid);
    char uuid_str[40] = {0};
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);
}

int set_cookie(std::string email, std::string& cookie)
{
    /*获取redis连接池*/
    CacheManager *cache_manager = CacheManager::getInstance();
    if (!cache_manager) {
        return -1;
    }
    /*token为缓冲池的名字*/
    CacheConn* cache_conn = cache_manager->GetCacheConn("token");
    if (!cache_conn) {
        return -1;
    }
    AUTO_REL_CACHECONN(cache_manager, cache_conn);
    
    if (!cache_conn) {
        LOG_ERROR << "get cache conn failed";
        return -1;
    }

    cookie = generate_uuid(); //cookie具有唯一性
    std::string ret = cache_conn->SetEx(cookie, 24 * 3600, email); //cookie有效期为24小时
    if (ret != "OK") {
        LOG_ERROR << "set cookie failed, ret: " << ret;
        return -1;
    }
    return 0;

}