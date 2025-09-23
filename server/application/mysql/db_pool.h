#ifndef __CHATROOM_SERVER_MYSQL_DB_POOL_H__
#define __CHATROOM_SERVER_MYSQL_DB_POOL_H__

#include <condition_variable>
#include <iostream>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <cstdint>
#include <mysql.h>

#define MAX_ESCAPE_STRING_LEN   10240

class CDBResultSet
{
public:
    CDBResultSet(MYSQL_RES *res);
    virtual ~CDBResultSet();

    bool Next();
    bool GetInt(const char* key, int& out);
    bool GetString(const char* key, char*& out);
private:
    int _GetIndex(const char* key);
    MYSQL_RES *res_;
    MYSQL_ROW row_;
    std::map<std::string, int> key_map_;

};

class CDBPrepareStatement
{
public:
    CDBPrepareStatement();
    virtual ~CDBPrepareStatement();

    bool Init(MYSQL* mysql, std::string& sql);
    void SetParam(uint32_t index, int& value);
    void SetParam(uint32_t index, uint32_t& value);
    void SetParam(uint32_t index, std::string& value);
    void SetParam(uint32_t index, const std::string& value);

    bool ExecuteUpdate();
    uint32_t GetInsertId();
private:
    MYSQL_STMT* stmt_;
    MYSQL_BIND* param_bind_;
    uint32_t param_cnt_;
};

class CDBPool;

class CDBConn
{
public:
    CDBConn(CDBPool *pDBPool);
    virtual ~CDBConn();
private:
    int row_num = 0;
    CDBPool *db_pool_;
    MYSQL* mysql_;
    /*
    escape_string_ 是一个字符数组缓冲区，
    用于存储经过转义处理后的字符串。
    转义处理的核心是通过 MySQL 提供的 mysql_real_escape_string 函数，
    将特殊字符转换为安全的表示形式
    （例如，单引号 ' 会被转义为 \'，反斜杠 \ 会被转义为 \\ 等）。
    */
    char escape_string_[MAX_ESCAPE_STRING_LEN + 1];
};

class CDBPool
{
public:
    CDBPool() {

    }
    CDBPool(const char* pool_name, const char* db_server_ip, 
                uint16_t db_server_port, const char* username, 
                const char* password, const char* db_name, 
                int max_conn_cnt);
    virtual ~CDBPool();

    int Init();
    CDBConn *GetDBConn(const int timeout_ms = 0); //获取连接资源
    void RelDBConn(CDBConn *pConn); //归还连接资源

    const char* GetPoolName() const {
        return pool_name_.c_str();
    }
    const char* GetDBServerIP() {
        return db_server_ip_.c_str();
    }
    uint16_t GetDBServerPort() {
        return db_server_port_;
    }
    const char* GetUsername() {
        return username_.c_str();
    }
    const char* GetPassword() {
        return password_.c_str();
    }
    const char* GetDBName() {
        return db_name_.c_str();
    }
private:
    std::string pool_name_;
    std::string db_server_ip_;
    uint16_t db_server_port_;
    std::string username_;
    std::string password_;
    std::string db_name_;
    int db_curr_conn_cnt_;
    int db_max_conn_cnt_;
    list<CDBConn*> free_list_;
    list<CDBConn*> used_list_;
    std::mutex mutex_;
    std::condition_variable cond_var_;
    bool abort_request_ = false;
};

class CDBManager
{
public:
    virtual ~CDBManager();
    static void SetConfPath(const char* conf_path);
    static CDBManager *getInstance();

    int Init();

    CDBConn *GetDBConn(const char *dbpool_name);
    void RelDBConn(CDBConn *pConn);

private:
    CDBManager();
private:
    static CDBManager *s_db_manager;
    map<string, CDBPool *> dbpool_map_;
    static std::string conf_path_;
};

class AutoRelDBCon
{
public:
    AutoRelDBCon(CDBManager *manager, CDBConn *conn) 
    : manager_(manager), conn_(conn) {

    }

    ~AutoRelDBCon() {
        if (manager_) {
            manager_->RelDBConn(conn_);
        }
    }
private:
    CDBManager *manager_ = NULL;
    CDBConn *conn_ = NULL;
};

#define AUTO_REL_DBCONN(manager, conn) AutoRelDBCon auto_rel_db_conn(manager, conn)

#endif
