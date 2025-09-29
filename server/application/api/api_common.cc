#include "api_common.h"
#include "api_reg.h"
#include "cache_pool.h"
#include "db_pool.h"

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
        LOG_ERROR << "get cache manager failed";
        return -1;
    }
    /*token为缓冲池的名字*/
    CacheConn* cache_conn = cache_manager->GetCacheConn("token");
    if (!cache_conn) {
        LOG_ERROR << "get cache conn failed";
        return -1;
    }
    AUTO_REL_CACHECONN(cache_manager, cache_conn);
    
    if (!cache_conn) {
        LOG_ERROR << "get cache conn failed";
        return -1;
    }

    cookie = generate_uuid(); //cookie具有唯一性
    LOG_INFO << "generate cookie: " << cookie << " for email: " << email;
    std::string ret = cache_conn->SetEx(cookie, 24 * 3600, email); //cookie有效期为24小时
    if (ret != "OK") {
        LOG_ERROR << "set cookie failed, ret: " << ret;
        return -1;
    }
    return 0;
}

int get_username_and_userid_by_email(std::string& email, std::string& username, int32_t& userid)
{
    int ret = 0;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn* db_conn = db_manager->GetDBConn("chatroom_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);

    std::string strsql = FormatString("select id, username from users where email='%s'", email.c_str());
    CDBResultSet *result_set = db_conn->ExecuteQuery(strsql.c_str());
    if (result_set && result_set->Next()) {
        username = result_set->GetString("username");
        userid = result_set->GetInt("id");

        LOG_INFO << "get username and userid by email from db, username: " << username << ", userid: " << userid;
        ret = 0;
    } else {
        ret = -1;
    }
    delete result_set;
    return ret;
}

int api_get_user_info_by_cookie(std::string& username, int32_t& userid, 
    std::string& email, std::string cookie)
{
    int ret = 0;
    CacheManager* cache_manager = CacheManager::getInstance();
    CacheConn* cache_conn = cache_manager->GetCacheConn("token");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);
    if (cache_conn) {
        email = cache_conn->Get(cookie);
        LOG_INFO << "get email from cache conn by cookie: " << email << ", cookie: " << cookie;
        if (email.empty()) {
            LOG_ERROR << "email not exists";
            return -1;
        } else {
            ret = get_username_and_userid_by_email(email, username, userid);
            if (ret == 0) {
                LOG_INFO << "get username and userid by email success";
                LOG_INFO << "username: " << username;
                LOG_INFO << "userid: " << userid;
            }
        }
    } else {
        LOG_ERROR << "api get user info by cookie failed, cannot get cache conn";
        ret = -1;
    }
    return ret;
}