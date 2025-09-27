#ifndef __CHATROOM_SERVER_APPLICATION_REDIS_CACHE_POOL_H__
#define __CHATROOM_SERVER_APPLICATION_REDIS_CACHE_POOL_H__

#include <string>
#include <map>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <list>
#include <vector>

#include "hiredis.h"

#define REDIS_COMMAND_SIZE 300 /* redis Command 指令最大长度 */
#define FIELD_ID_SIZE 100      /* redis hash表field域字段长度 */
#define VALUES_ID_SIZE 1024    /* redis        value域字段长度 */
typedef char (*RFIELDS)[FIELD_ID_SIZE]; /* redis hash表存放批量field字符串数组类型 */
//数组指针类型，其变量指向 char[1024]
typedef char (*RVALUES)[VALUES_ID_SIZE]; /* redis 表存放批量value字符串数组类型 */

class CachePool;

class CacheConn
{
public:
    CacheConn(const char* server_ip, int server_port, int db_index, 
              const char* password, const char* pool_name = "");
    CacheConn(CachePool* pCachePool);
    virtual ~CacheConn();

    const char* GetPoolName() { return pool_name_.c_str(); }
    bool Init();
    void DeInit();
    std::string Get(std::string key);
    std::string Set(std::string key, std::string value);
    std::string SetEx(std::string key, int timeout, std::string value);
    bool MGet(const std::vector<std::string>& keys, 
        std::map<std::string, std::string>& ret_value);
    bool IsExists(std::string& key);
    long Del(std::string key);
    long Expire(std::string key, int timeout);
    long HDel(std::string key, std::string field);
    std::string HGet(std::string key, std::string field);
    bool HGet(std::string key, char* field, char* value);
    bool HGetAll(std::string key, 
        std::map<std::string, std::string>& ret_value);
    long HSet(std::string key, std::string field, std::string value);
    long HIncrBy(std::string key, std::string field, long value);
    long IncrBy(std::string key, long value);
    std::string HMSet(std::string key, 
        const std::map<std::string, std::string>& hash);
    bool HMGet(std::string key, std::list<std::string>& fields, 
        std::list<std::string>& ret_value);
    bool Incr(std::string key, int64_t& value);
    bool Decr(std::string key, int64_t& value);
    long LPush(std::string key, std::string value);
    long RPush(std::string key, std::string value);
    long LLen(std::string key);
    bool LRange(std::string key, long start, long end, 
        std::list<std::string> &ret_value);
    bool ZSetExist(std::string key, std::string member);
    int ZSetAdd(std::string key, long score, std::string member);
    int ZSetRem(std::string key, std::string member);
    int ZSetIncr(std::string key, std::string member);
    int ZSetCard(std::string key);
    int ZSetRevRange(std::string key, int start, int end, RVALUES values, 
        int& get_num);
    int ZSetGetScore(std::string key, std::string member);
    bool GetXRevRange(const std::string& key, const std::string& start, 
        const std::string& end, int count, 
        std::vector<std::pair<std::string, std::string>>& msgs);
    bool XAdd(const std::string& key, std::string& id, 
        const std::vector<std::pair<std::string, std::string>>& field_values);
    bool FlushDb();

    
private:
    std::string pool_name_; // 连接池名称
    std::string server_ip_; // redis服务器ip
    int server_port_;       // redis服务器端口
    int db_index_;         // redis数据库索引
    std::string password_;  // redis密码
    redisContext* context_; // redis上下文
    uint64_t last_connect_time_; // 上次连接时间
    CachePool* cache_pool_; // 连接所属的连接池

};

class CachePool
{
public:
    CachePool(const char* pool_name, const char* server_ip,
              int server_port, int db_index,
              const char* password, int max_conn_cnt);
    ~CachePool();

    bool Init();
    CacheConn* GetCacheConn(const int timeout_ms = 0);
    void RelCacheConn(CacheConn* cache_conn);
    const char* GetPoolName() { return pool_name_.c_str(); }
    const char* GetServerIP() { return server_ip_.c_str(); }
    int GetServerPort() { return server_port_; }
    int GetDBIndex() { return db_index_; }
    const char* GetPassword() { return password_.c_str(); }
    int GetMaxConnCnt() { return max_conn_cnt_; }
    int GetCurConnCnt() { return cur_conn_cnt_; }
    void IncCurConnCnt() { cur_conn_cnt_++; }
    void DecCurConnCnt() { cur_conn_cnt_--; }
    bool IsAbortRequest() { return abort_request_; }
    void SetAbortRequest(bool abort_request) { abort_request_ = abort_request; }
private:
    std::string pool_name_;
    std::string server_ip_;
    int server_port_;
    int db_index_;
    std::string password_;
    int max_conn_cnt_;
    int cur_conn_cnt_;
    bool abort_request_;
    std::mutex mutex_;
    std::condition_variable cond_var_;
    std::list<CacheConn*> free_list_;
};

class CacheManager
{
public:
    virtual ~CacheManager();
    static void SetConfPath(const char* conf_path);
    static CacheManager *getInstance();
    bool Init();
    CacheConn* GetCacheConn(const char* pool_name);
    void RelCacheConn(CacheConn* cache_conn);
private:
    CacheManager();
private:
    std::map<std::string, CachePool*> cache_pool_map_;
    static std::string conf_path_;
};

class AutoRelCacheCon {
  public:
    AutoRelCacheCon(CacheManager *manger, CacheConn *conn)
        : manger_(manger), conn_(conn) {}
    ~AutoRelCacheCon() {
        if (manger_) {
            manger_->RelCacheConn(conn_);
        }
    } //在析构函数规划
  private:
    CacheManager *manger_ = NULL;
    CacheConn *conn_ = NULL;
};

#define AUTO_REL_CACHECONN(m, c) AutoRelCacheCon autorelcacheconn(m, c)

#endif
