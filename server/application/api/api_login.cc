#include "api_login.h"
#include "db_pool.h"
#include "util.h"
#include "api_common.h"
#include "muduo/base/Logging.h"
#include "muduo/base/md5.h"
#include <json/json.h>

int decode_login_json(const std::string &str_json, std::string& email, std::string& password)
{
    bool res;
    Json::Reader reader;
    Json::Value root;
    res = reader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse register json failed";
        return -1;
    }

    if (root["email"].isNull()) {
        LOG_ERROR << "email is null";
        return -1;
    }

    email = root["email"].asString();

    if (root["password"].isNull()) {
        LOG_ERROR << "password is null";
        return -1;
    }
    password = root["password"].asString();

    return 0;
}

int encode_login_json(api_error_id input, std::string message, std::string& str_json)
{
    Json::Value root;
    root["id"] = api_error_id_to_string(input);
    root["message"] = message;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

int verify_user_password(std::string& email, std::string& password)
{
    int ret = -1;
    CDBManager* db_manager = CDBManager::getInstance();
    CDBConn* db_conn = db_manager->GetDBConn("chatroom_slave");
    AUTO_REL_DBCONN(db_manager, db_conn);
    if (!db_conn) {
        LOG_ERROR << "GetDBConn failed";
        return -1;
    }

    std::string strsql = FormatString("select username, password_hash, salt from users where email='%s'", email.c_str());
    CDBResultSet* result_set = db_conn->ExecuteQuery(strsql.c_str());
    if (result_set && result_set->Next()) {
        std::string db_username = result_set->GetString("username");
        std::string db_password_hash = result_set->GetString("password_hash");
        std::string db_salt = result_set->GetString("salt");
        MD5 md5(password + db_salt);
        std::string client_password_hash = md5.toString();
        if (db_password_hash == client_password_hash) {
            LOG_INFO << "username: " << db_username << "verify success";
            ret = 0;
        } else {
            LOG_ERROR << "db_password_hash not equal client_password_hash";
            ret = -1;
        }
    } else {
        LOG_ERROR << "result set or result set content is NULL";
        ret = -1;
    }
    if (result_set) {
        delete result_set;
    }
    return ret;
}

int api_login_user(std::string& post_data, std::string& resp_json)
{
    int ret = -1;
    std::string email;
    std::string password;

    /*json解析*/
    if (decode_login_json(post_data, email, password) < 0) {
        LOG_ERROR << "decode login json failed";
        encode_login_json(api_error_id::bad_request, "email or password not filled", resp_json);
        return -1;
    }

    /*校验邮箱 密码*/
    ret = verify_user_password(email, password);
    if (ret == 0) {
        set_cookie(email, resp_json);
    } else {
        encode_login_json(api_error_id::bad_request, "email password verify failed", resp_json);
    }
    return ret;

}