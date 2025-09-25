#include "db_pool.h"
#include <string>
#include "muduo/base/Logging.h"
#include "config_file_reader.h"

#define MIN_DB_CONN_CNT 1
#define MAX_DB_CONN_FAIL_NUM    10

CDBResultSet::CDBResultSet(MYSQL_RES *res)
{
    /*保存底层结果集对象*/
    res_ = res;
    /*获取结果集中的字段（列）的数量*/
    int num_fields = mysql_num_fields(res_);
    /*获取所有字段的元信息（字段名，类型等）*/
    MYSQL_FIELD *fields = mysql_fetch_fields(res_);

    for (int i = 0; i < num_fields; i++) {
        key_map_.insert(make_pair(fields[i].name, i));
        LOG_DEBUG << "CDBResultSet: num_fields = " << num_fields << " fields[" << i << "].name: " << fields[i].name;

    }
}

CDBResultSet::~CDBResultSet()
{
    if (res_) {
        mysql_free_result(res_);
        res_ = NULL;
    }
}

/*遍历结果行*/
bool CDBResultSet::Next()
{
    /*MYSQL_ROW本质是一个字符串数组*/
    row_ = mysql_fetch_row(res_); /*获取结果集的下一行*/
    if (row_) {
        return true;
    } else {
        return false;
    }
}

int CDBResultSet::_GetIndex(const char* key)
{
    std::map<std::string, int>::iterator it = key_map_.find(key);
    if (it == key_map_.end()) {
        return -1; /*字段名不存在*/
    } else {
        return it->second; /*返回字段对应的列索引*/
    }
}

bool CDBResultSet::GetInt(const char* key, int& out)
{
    int idx = _GetIndex(key);
    if (idx == -1) {
        return false;
    } else {
        out = atoi(row_[idx]);
        return true;
    }
}

bool CDBResultSet::GetString(const char* key, char*& out)
{
    int idx = _GetIndex(key);
    if (idx == -1) {
        return false;
    } else {
        out = row_[idx];
        return true;
    }
}

CDBPrepareStatement::CDBPrepareStatement()
{
    stmt_ = NULL;
    param_bind_ = NULL;
    param_cnt_ = 0;
}

CDBPrepareStatement::~CDBPrepareStatement()
{
    if (stmt_) {
        mysql_stmt_close(stmt_);
        stmt_ = NULL;
    }

    if (param_bind_) {
        delete[] param_bind_;
        param_bind_ = NULL;
    }
}

bool CDBPrepareStatement::Init(MYSQL* mysql, std::string& sql)
{
    mysql_ping(mysql);

    /*初始化预处理句柄*/
    stmt_ = mysql_stmt_init(mysql);
    if (!stmt_) {
        LOG_ERROR << "mysql_stmt_init failed";
        return false;
    }

    /*预处理 SQL 模板（编译 SQL）*/
    if (mysql_stmt_prepare(stmt_, sql.c_str(), sql.size())) {
        LOG_ERROR << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt_);
        return false;
    }

    /*获取参数数量（即 SQL 中 ? 的个数）*/
    param_cnt_ = mysql_stmt_param_count(stmt_);
    if (param_cnt_ > 0) {
        param_bind_ = new MYSQL_BIND[param_cnt_];
        if (!param_bind_) {
            LOG_ERROR << "new MYSQL_BIND[param_cnt_] failed";
            return false;
        }

        memset(param_bind_, 0, sizeof(MYSQL_BIND) * param_cnt_);
    }
    return true;
}

void CDBPrepareStatement::SetParam(uint32_t index, int& value)
{
    if (index >= param_cnt_) {
        LOG_ERROR << "index out of range[" << param_cnt_ << "]: " << index;
        return;
    }

    param_bind_[index].buffer_type = MYSQL_TYPE_LONG; // 指定类型为32位整数
    param_bind_[index].buffer = &value;
}

void CDBPrepareStatement::SetParam(uint32_t index, uint32_t& value)
{
    if (index >= param_cnt_) {
        LOG_ERROR << "index out of range[" << param_cnt_ << "]: " << index;
        return;
    }

    param_bind_[index].buffer_type = MYSQL_TYPE_LONG; // 指定类型为32位整数
    param_bind_[index].buffer = &value;
}

void CDBPrepareStatement::SetParam(uint32_t index, std::string& value)
{
    if (index >= param_cnt_) {
        LOG_ERROR << "index out of range[" << param_cnt_ << "]: " << index;
        return;
    }

    param_bind_[index].buffer_type = MYSQL_TYPE_STRING;
    param_bind_[index].buffer = (char*)value.c_str();
    param_bind_[index].buffer_length = value.size();
}

void CDBPrepareStatement::SetParam(uint32_t index, const std::string& value)
{
    if (index >= param_cnt_) {
        LOG_ERROR << "index out of range[" << param_cnt_ << "]: " << index;
        return;
    }

    param_bind_[index].buffer_type = MYSQL_TYPE_STRING;
    param_bind_[index].buffer = (char*)value.c_str();
    param_bind_[index].buffer_length = value.size();
}

bool CDBPrepareStatement::ExecuteUpdate()
{
    // 检查预处理句柄是否有效
    if (!stmt_) { 
        LOG_ERROR << "CDBPrepareStatement::ExecuteUpdate failed, no stmt";
        return false;
    }

    // 将参数绑定到预处理语句
    if (mysql_stmt_bind_param(stmt_, param_bind_)) {
        LOG_ERROR << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt_);
        return false;
    }

    // 执行预处理语句
    if (mysql_stmt_execute(stmt_)) {
        LOG_ERROR << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt_);
        return false;
    }

    // 检查受影响的行数（若为 0，视为执行失败，适合需要确保修改生效的场景）
    if (mysql_stmt_affected_rows(stmt_) == 0) {
        LOG_ERROR << "ExecuteUpdate effects no row";
        return false;
    }

    return true;

}

/*
GetInsertId() 方法的有效性不取决于主键类型，而取决于表中是否存在 AUTO_INCREMENT 列：
若存在 AUTO_INCREMENT 列（无论是否是主键），返回该列的自增值；
若不存在 AUTO_INCREMENT 列，返回 0 或无意义的值，无法用于获取主键。
*/
/*返回最后一次 INSERT 操作产生的自增主键值*/
uint32_t CDBPrepareStatement::GetInsertId()
{
    return mysql_stmt_insert_id(stmt_);
}

CDBPool::CDBPool(const char* pool_name, const char* db_server_ip, 
                uint16_t db_server_port, const char* username, 
                const char* password, const char* db_name, 
                int max_conn_cnt)
{
    pool_name_ = pool_name;
    db_server_ip_ = db_server_ip;
    db_server_port_ = db_server_port;
    username_ = username;
    password_ = password;
    db_name_ = db_name;
    db_max_conn_cnt_ = max_conn_cnt;
    db_cur_conn_cnt = MIN_DB_CONN_CNT;

}

CDBPool::~CDBPool()
{
    std::lock_guard<std::mutex> lock(mutex_);
    abort_request_ = true;
    cond_var_.notify_all();
    for (list<CDBConn *>::iterator it = free_list_.begin(); 
        it != free_list_.end(); it++) {
        CDBConn *pConn = *it;
        delete pConn;
    }

    free_list_.clear();
}

int CDBPool::Init()
{
     for (int i = 0; i < db_cur_conn_cnt_; i++) {
        CDBConn *db_conn = new CDBConn(this);
        int ret = db_conn->Init();
        if (ret) {
            delete db_conn;
            return ret;
        }

        free_list_.push_back(db_conn);
     }

     return 0;
}

CDBConn* CDBPool::GetDBConn(const int timeout_ms)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (abort_request) {
        LOG_WARN << "CDBPool is aborting...";
        return NULL;
    }

    if (free_list_.empty()) { //没有连接可以用
        //当前连接数量已经达到了最大连接数量
        if (db_cur_conn_cnt_ >= db_max_conn_cnt_) {
            // 查看是否需要超时等待
            if (timeout_ms <= 0) { //死等，直到有连接可用或者连接池退出
                cond_var_.wait(lock, [this] {
                    return (!free_list_.empty()) | abort_request_;
                });
            } else {
                // 超时等待
                cond_car_.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                [this] { return (!free_list_.empty()) | abort_request_; });
                //超时或者free_list不是empty或者abort_request
                if (free_list_.empty()) {
                    //超时或者abort_request
                    return NULL;
                }
            }

            if (abort_request_) {
                LOG_WARN << "CDBPool is aborting...";
                return NULL;
            }
        } else {
            //当前连接数量没有达到最大连接数量，创建新的连接
            CDBConn *db_conn = new CDBConn(this);
            int ret = db_conn->Init();
            if (ret) {
                LOG_ERROR << "CDBConn init failed";
                delete db_conn;
                return NULL;
            } else {
                free_list_.push_back(db_conn);
                db_cur_conn_cnt_++;
            }
        }
    }

    CDBConn *db_conn = free_list_.front();
    free_list_.pop_front();
    return db_conn;
}

void CDBPool::RelDBConn(CDBConn *pConn)
{
    std::lock_guard<std::mutex> lock(mutex_);
    list<CDBConn *>::iterator it = free_list_.begin();
    for (;it != free_list_.end(); it++) {
        if (*it == pConn) {
            LOG_WARN << "RelDBConn failed, conn has been in free_list";
            return;
        }
    }

    if (it == free_list_.end()) {
        free_list_.push_back(pConn);
        cond_var_.notify_one();
    } else {
        LOG_WARN << "RelDBConn failed";
    }
}

CDBConn::CDBConn(CDBPool *pPool) {
    db_pool = pPool;
    mysql_ = NULL;
}

CDBConn::~CDBConn() {
    if (mysql_) {
        mysql_close(mysql_);
    }
}

int CDBConn::Init()
{
    /*初始化一个MYSQL结构体，用于后续所有MySQL操作的“句柄”*/
    mysql_ = mysql_init(NULL);
    if (!mysql_) {
        LOG_ERROR << "musql_init failed";
        return 1;
    }
    /*mysql_options用于在建立连接前设置MySQL连接的额外选项，
    需要在mysql_real_connect之前调用*/
    int reconnect = 1;
    // mysql_options(mysql_, MYSQL_OPT_RECONNECT, (char *)&reconnect);
    /*MySQL中的utf8实际上是utf8mb3的别名，
    仅支持最多 3 字节的 Unicode 字符（如常见中文、英文），
    但不支持 4 字节的字符（如 Emoji 表情、某些罕见文字）。
    而 utf8mb4 支持 4 字节字符，能兼容所有 Unicode 字符，
    避免因字符不支持导致的插入失败或乱码。*/
    mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    if (!mysql_real_connect(mysql_, db_pool_->GetDBServerIP(), 
                            db_pool_->GetUsername(), 
                            db_pool_->GetPassword(),
                            db_pool_->GetDBName(),
                            db_pool_->GetDBServerPort(), NULL, 0)) {
        LOG_ERROR << "mysql_real_connect failed, " << mysql_error(mysql_);
        return 2;
    }
    return 0;
}

const char* CDBConn::GetPoolName()
{
    return db_pool_->GetPoolName();
}

/*专门用于执行 CREATE 类语句（如创建表、索引等），属于 DDL（数据定义语言）。*/
bool CDBConn::ExecuteCreate(const char* sql_query)
{
    mysql_ping(mysql_);/*检测连接状态，若连接断开，在某些配置下会自动重连（确保操作前连接有效）。*/
    if (mysql_real_query(mysql_, sql_query, strlen(sql_query))) {
        LOG_ERROR << "CDBConn::ExecuteCreate: mysql_real_query failed: " << mysql_error(mysql_);
        return false;
    }
    return true;
}

/*专门用于执行 DROP 类语句（如删除表、索引等），也属于 DDL。*/
bool CDBConn::ExecuteDrop(const char* sql_query)
{
    mysql_ping(mysql_);/*检测连接状态，若连接断开，在某些配置下会自动重连（确保操作前连接有效）。*/
    if (mysql_real_query(mysql_, sql_query, strlen(sql_query))) {
        LOG_ERROR << "CDBConn::ExecuteDrop: mysql_real_query failed: " << mysql_error(mysql_);
        return false;
    }
    return true;
}
/*通常用于执行其他无返回结果的通用 SQL（可能包括 TRUNCATE、ALTER 等，或业务特定的非查询语句）。*/
bool CDBConn::ExecutePassQuery(const char* sql_query)
{
    mysql_ping(mysql_);
    if (mysql_real_query(mysql_, sql_query, strlen(sql_query))) {
        LOG_ERROR << "CDBConn::ExecutePassQuery: mysql_real_query failed: " << mysql_error(mysql_);
        return false;
    }
    return true;
}



CDBManager::CDBManager() 
{

}

CDBManager::~CDBManager() 
{
    
}

CDBManager *CDBManager::getInstance()
{
    static CDBManager instance;
    static bool inited = instance.Init();
    if (!inited) {
        return nullptr;
    }
    return &instance;
    // if (!s_db_manager) {
    //     s_db_manager = new CDBManager();
    //     if (s_db_manager->Init()) {
    //         delete s_db_manager;
    //         s_db_manager = NULL;
    //     }
    // }
    // return s_db_manager;
}

void CDBManager::SetConfPath(const char* conf_path)
{
    conf_path_ = conf_path;
}

bool CDBManager::Init()
{
    LOG_INFO << "CDBManager Init";
    CConfigFileReader config_file(conf_path_.c_str());
    
    char *db_instances = config_file.GetConfigValue("DBInstances");
    if (!db_instances) {
        LOG_ERROR << "DBInstances not found in config file";
        return false;
    }

    char host[64];
    char port[64];
    char dbname[64];
    char username[64];
    char password[64];
    char maxconncnt[64];
    CStrExplode instances_name(db_instances, ',');

    for (uint32_t i = 0; i < instances_name.GetItemCnt(); i++) {
        char* pool_name = instances_name.GetItem(i);
        snprintf(host, 64, "%s_host", pool_name);
        snprintf(port, 64, "%s_port", pool_name);
        snprintf(dbname, 64, "%s_dbname", pool_name);
        snprintf(username, 64, "%s_username", pool_name);
        snprintf(password, 64, "%s_password", pool_name);
        snprintf(maxconncnt, 64, "%s_maxconncnt", pool_name);

        char *db_host = config_file.GetConfigValue(host);
        char *str_db_port = config_file.GetConfigValue(port);
        char *db_dbname = config_file.GetConfigValue(dbname);
        char *db_username = config_file.GetConfigValue(username);
        char *db_password = config_file.GetConfigValue(password);
        char *str_maxconncnt = config_file.GetConfigValue(maxconncnt);

        LOG_INFO << "CDBManager::Init()";
        LOG_INFO << "db_host: " << db_host << ", db_port:" << str_db_port << 
                ", db_dbname:" << db_dbname << ", db_username:" << db_username << 
                ", db_password: " << db_password << ", str_maxconncnt: " << str_maxconncnt;

        if (!db_host || !str_db_port || !db_dbname || !db_username || 
            !db_password || !str_maxconncnt) {
            LOG_ERROR << "Invalid Configure DB Instance: " << pool_name;
            return false;
        }

        int db_port = atoi(str_db_port);
        int db_maxconncnt = atoi(str_maxconncnt);
        CDBPool *pDBPool = new CDBPool(pool_name, db_host, db_port, db_username,
                                        db_password, db_dbname, db_maxconncnt);
        if (pDBPool->Init()) {
            LOG_ERROR << "Init DB Instance " << pool_name << " failed, pDBPool->Init() failed";
            return false;
        }
        dbpool_map_.insert(make_pair(pool_name, pDBPool));
    }
    return true;

}

CDBConn *CDBManager::GetDBConn(const char* dbpool_name) 
{
    std::map<std::string, CDBPool*>::iterator it = dbpool_map_.find(dbpool_name);
    if (it == dbpool_map_.end()) {
        return NULL;
    } else {
        return it->second->GetDBConn();
    }
}

void CDBManager::RelDBConn(CDBConn *pConn)
{
    if (!pConn) {
        return;
    }

    std::map<std::string, CDBPool*>::iterator it = dbpool_map_.find(pConn->GetPoolName());
    if (it != dbpool_map_.end()) {
        it->second->RelDBConn(pConn);
    }
}
