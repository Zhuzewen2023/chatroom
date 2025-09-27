#include "api_reg.h"
#include <json/json.h>
#include "api_common.h"
#include "util.h"
#include "config_file_reader.h"
#include "db_pool.h"
#include "muduo/base/md5.h"

int decode_register_json(const std::string &str_json, string& username, string& email, string& password)
{
    bool res;
    Json::Reader reader;
    Json::Value root;
    res = reader.parse(str_json, root);
    if (!res) {
        LOG_ERROR << "parse register json failed";
        return -1;
    }
    if (root["username"].isNull()) {
        LOG_ERROR << "username is null";
        return -1;
    }
    username = root["username"].asString();
    
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

int encode_register_json(api_error_id input, string message, string& str_json)
{
    Json::Value root;
    root["id"] = api_error_id_to_string(input);
    root["message"] = message;
    Json::FastWriter writer;
    str_json = writer.write(root);
    return 0;
}

int register_user(string& username ,string& email, string& password, api_error_id& error_id)
{
    int ret = -1;
    CDBManager *db_manager = CDBManager::getInstance();
    CDBConn *db_conn = db_manager->GetDBConn("chatroom_master");
    AUTO_REL_DBCONN(db_manager, db_conn); //RAII归还连接给连接池
    if (!db_conn) {
        LOG_ERROR << "GetDBConn(chatroom_master) failed";
        return -1;
    }
    // 先查询用户名 邮箱是否存在，如果存在就报错
    std::string str_sql = FormatString("select id, username, email from users where username='%s' or email='%s'", username.c_str(), email.c_str());
    CDBResultSet *result_set = db_conn->ExecuteQuery(str_sql.c_str());
    if (result_set && result_set->Next()) {
        if (result_set->GetString("username")) {
            if (result_set->GetString("username") == username) {
                error_id = api_error_id::username_exists;
                LOG_WARN << "id: " << result_set->GetInt("id") << ", username: " << username << " 已经存在";    
            }
        }
        if (result_set->GetString("email")) {
            if (result_set->GetString("email") == email) {
                error_id = api_error_id::email_exists;
                LOG_WARN << "id: " << result_set->GetInt("id") << ", email: " << email << " 已经存在";
            }
        }
        delete result_set;
        return -1;
    }

    /*用户不存在，注册账号*/
    /*随机数生成salt值*/
    std::string salt = RandomString(16);
    MD5 md5(password + salt);
    string password_hash = md5.toString();
    LOG_INFO << "register_user: salt = " << salt;
    
    /*插入语句*/
    str_sql = "INSERT INTO `users` (`username`, `email`, `password_hash`, `salt`) VALUES(?, ?, ?, ?)";
    LOG_INFO << "Execute: " << str_sql;
    CDBPrepareStatement *stmt = new CDBPrepareStatement();
    if (stmt->Init(db_conn->GetMysql(), str_sql)) {
        uint32_t index = 0;
        stmt->SetParam(index++, username);
        stmt->SetParam(index++, email);
        stmt->SetParam(index++, password_hash);
        stmt->SetParam(index++, salt);
        bool bRet = stmt->ExecuteUpdate();
        if (bRet) {
            ret = 0;
            uint32_t user_id = db_conn->GetInsertId();
            LOG_INFO << "insert user_id " << user_id << ", username: " << username;
        } else {
            LOG_ERROR << "insert users failed. " << str_sql;
            ret = -1;
        }
    }
    delete stmt;
    return ret;

}

int api_register_user(std::string &post_data, std::string &response_data)
{
    //解析 json username email password
    std::string username;
    std::string email;
    std::string password;
    //json反序列化
    /*从json获取username, email, password*/
    int ret = decode_register_json(post_data, username, email, password);
    if (ret < 0) {
        encode_register_json(api_error_id::bad_request, "请求参数不全", response_data);
        return -1;
    }
    //封装register_user(username email password)
    api_error_id error_id = api_error_id::bad_request;
    /*将用户信息插入MySQL*/
    ret = register_user(username, email, password, error_id);
    //返回注册结果
    if (ret == 0) {
        /*注册成功需要产生cookie*/
        /*生成cookie填充resp_data*/
        set_cookie(email, response_data);
    } else {
        /*将error_id和第二个参数的string组织为json字符串填充response_data*/
        encode_register_json(error_id, api_error_id_to_string(error_id), response_data);
    }

    //异常注册 400 Bad Request {"id":"USERNAME_EXISTS", "message": ""} {"id":"EMAIL_EXISTS", "message": ""}
    return ret;
}