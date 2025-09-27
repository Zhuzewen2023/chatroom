#include "cache_pool.h"
#include "config_file_reader.h"
#include "muduo/base/Logging.h"

#include <vector>

#define MIN_CACHE_CONN_CNT 2
#define MAX_CACHE_CONN_FAIL_NUM 10

CacheConn::CacheConn(const char* server_ip, int server_port, int db_index,
                     const char* password, const char* pool_name) 
{
    server_ip_ = server_ip;
    server_port_ = server_port;

    db_index_ = db_index;
    password_ = password;
    pool_name_ = pool_name;
    context_ = NULL;
    last_connect_time_ = 0;
}

CacheConn::CacheConn(CachePool* pCachePool) 
{
    cache_pool_ = pCachePool;
    if (pCachePool) {
        server_ip_ = pCachePool->GetServerIP();
        server_port_ = pCachePool->GetServerPort();
        db_index_ = pCachePool->GetDBIndex();
        password_ = pCachePool->GetPassword();
        pool_name_ = pCachePool->GetPoolName();
    } else {
        LOG_ERROR << "pCachePool is NULL";
    }

    context_ = NULL;
    last_connect_time_ = 0;
}

CacheConn::~CacheConn() 
{
    if (context_) {
        redisFree(context_);
        context_ = NULL;
    }
}

bool CacheConn::Init()
{
    /*context非空，连接正常*/
    if (context_) {
        LOG_INFO << "CacheConn::Init, context_ is not null";
        return true;
    }
    uint64_t cur_time = (uint64_t)time(NULL);
    if (cur_time < last_connect_time_ + 1) {
        LOG_INFO << "last connect time: " << last_connect_time_ << ", cur time: " << cur_time;
        return false;
    }
    last_connect_time_ = cur_time;
    struct timeval timeout = {0, 1000000}; /*1000ms超时*/
    context_ = redisConnectWithTimeout(server_ip_.c_str(), server_port_, timeout);
    if (!context_ || context_->err) {
        if (context_) {
            LOG_ERROR << "redisConnect failed: " << context_->errstr;
            redisFree(context_);
            context_ = NULL;
        } else {
            LOG_ERROR << "redisConnect failed";
        }
        return false;
    }
    LOG_INFO << "CacheConn::Init, connect to redis server success, ip: "
             << server_ip_ << ", port: " << server_port_ << ", db_index_: " << db_index_
             << ", pool_name_: " << pool_name_;
    redisReply* reply;
    /*验证*/
    if (!password_.empty()) {
        reply = (redisReply*)redisCommand(context_, "AUTH %s", password_.c_str());
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            LOG_ERROR << "Authentication failure: " << (reply ? reply->str : "null");
            if (reply)
                freeReplyObject(reply);
            return false;
        } else {
            LOG_INFO << "Authentication success";
        }
        freeReplyObject(reply);
    }

    reply = (redisReply*)redisCommand(context_, "SELECT %d", db_index_);
    if (reply && (reply->type == REDIS_REPLY_STATUS) && (strncmp(reply->str, "OK", 2) == 0)) {
        LOG_INFO << "SELECT db_index success: " << db_index_;
        freeReplyObject(reply);
        return true;
    } else {
        if (reply) {
            LOG_ERROR << "SELECT db_index failed: " << reply->str;
            freeReplyObject(reply);
        }
        return false;
    }

}

void CacheConn::DeInit()
{
    if (context_) {
        redisFree(context_);
        context_ = NULL;
    }
}

std::string CacheConn::Get(std::string key)
{
    std::string ret_value;
    if (!Init()) {
        return ret_value;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "GET %s", key.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return ret_value;
    }

    if (reply->type == REDIS_REPLY_STRING) {
        ret_value.append(reply->str, reply->len);
    } else if (reply->type == REDIS_REPLY_NIL) {
        // key不存在
        ret_value = "";
    } else {
        LOG_ERROR << "GET: " << key << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        ret_value = "";
    }

    freeReplyObject(reply);
    return ret_value;
}

std::string CacheConn::Set(std::string key, std::string value)
{
    std::string ret_value;
    if (!Init()) {
        return ret_value;
    }
    redisReply *reply = (redisReply*)redisCommand(context_, "SET %s %s",
                                                   key.c_str(), value.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return ret_value;
    }
    if (reply->type == REDIS_REPLY_STATUS) {
        ret_value.append(reply->str, reply->len);
    } else {
        LOG_ERROR << "SET: " << key << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        ret_value = "";
    }
    freeReplyObject(reply); // 释放资源
    return ret_value;
}

std::string CacheConn::SetEx(std::string key, int timeout, std::string value)
{
    std::string ret_value;
    if (!Init()) {
        return ret_value;
    }

    redisReply *reply = (redisReply *)redisCommand(
        context_, "SETEX %s %d %s", key.c_str(), timeout, value.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return ret_value;
    }
    if (reply->type == REDIS_REPLY_STATUS) {
        ret_value.append(reply->str, reply->len);
    } else {
        LOG_ERROR << "SETEX: " << key << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        ret_value = "";
    }
    freeReplyObject(reply);
    return ret_value;
}

bool CacheConn::MGet(const std::vector<std::string>& keys, std::map<std::string, std::string>& ret_value)
{
    if (!Init()) {
        return false;
    }
    if (keys.size() == 0) {
        return true;
    }
    std::string str_key;
    bool first = true;
    for (std::vector<std::string>::const_iterator it = keys.begin(); it != keys.end(); it++) {
        if (first) {
            first = false;
            str_key = *it;
        } else {
            str_key += " " + *it;
        }
    }

    if (str_key.empty()) {
        return false;
    }
    str_key = "MGET " + str_key;
    redisReply *reply = (redisReply *)redisCommand(context_, str_key.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    if (reply->type == REDIS_REPLY_ARRAY) {
        for(size_t i = 0; i < reply->elements; i++) {
            redisReply *value_reply = reply->element[i];
            if (value_reply->type == REDIS_REPLY_STRING) {
                ret_value[keys[i]] = value_reply->str;
            }
        }
    }
    freeReplyObject(reply);
    return true;

}

bool CacheConn::IsExists(std::string& key)
{
    if (!Init()) {
        return false;
    }

    redisReply *reply = (redisReply*)redisCommand(context_, "EXISTS %s", key.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    bool ret_value = (reply->integer == 1);
    freeReplyObject(reply);
    return ret_value;
}

long CacheConn::Del(std::string key)
{
    if (!Init()) {
        return -1;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "DEL %s", key.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }

    long ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

long CacheConn::Expire(std::string key, int timeout)
{
    if (!Init()) {
        return -1;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "EXPIRE %s %d", key, timeout);
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }
    long ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

long CacheConn::HDel(std::string key, std::string field)
{
    if (!Init()) {
        return -1;
    }
    redisReply* reply = (redisReply*)redisCommand(context_, "HDEL %s %s",
                                                   key.c_str(), field.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }

    long ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

std::string CacheConn::HGet(std::string key, std::string field)
{
    std::string ret_value;
    if (!Init()) {
        return ret_value;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "HGET %s %s",
                                                   key.c_str(), field.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return ret_value;
    }

    if (reply->type == REDIS_REPLY_STRING) {
        ret_value.append(reply->str, reply->len);
    } else if (reply->type == REDIS_REPLY_NIL) {
        // key不存在
        ret_value = "";
    } else {
        LOG_ERROR << "HGET: " << key << ", field: " << field << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        ret_value = "";
    }

    freeReplyObject(reply);
    return ret_value;
}

bool CacheConn::HGet(std::string key, char* field, char* value)
{
    if (!Init()) {
        return false;
    }
    redisReply* reply = (redisReply*)redisCommand(context_, "HGET %s %s",
                                                   key.c_str(), field);
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    if (reply->type == REDIS_REPLY_STRING) {
        strncpy(value, reply->str, reply->len);
        value[reply->len] = '\0';
    } else if (reply->type == REDIS_REPLY_NIL) {
        value[0] = '\0';
    } else {
        LOG_ERROR << "HGET: " << key << ", field: " << field << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        value[0] = '\0';
    }
    freeReplyObject(reply);
    return true;
}

bool CacheConn::HGetAll(std::string key, std::map<std::string, std::string>& ret_value)
{
    if (!Init()) {
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "HGETALL %s", key.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }

    if ((reply->type == REDIS_REPLY_ARRAY) && (reply->elements % 2 == 0)) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            redisReply* field_reply = reply->element[i];
            redisReply* value_reply = reply->element[i + 1];

            std::string field(field_reply->str, field_reply->len);
            std::string value(value_reply->str, value_reply->len);
            ret_value.insert(std::make_pair(field, value));
        }
    }

    freeReplyObject(reply);
    return true;
}

long CacheConn::HSet(std::string key, std::string field, std::string value)
{
    if (!Init()) {
        return -1;
    }

    redisReply* reply = (redisReply*)redisCommand(
        context_, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }

    long ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

long CacheConn::HIncrBy(std::string key, std::string field, long value)
{
    if (!Init()) {
        return -1;
    }
    redisReply* reply = (redisReply*)redisCommand(
        context_, "HINCRBY %s %s %ld", key.c_str(), field.c_str(), value);
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }
    long ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

long CacheConn::IncrBy(std::string key, long value)
{
    if (!Init()) {
        return -1;
    }

    redisReply* reply = (redisReply*)redisCommand(
        context_, "INCRBY %s %ld", key.c_str(), value);
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }

    long ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

std::string CacheConn::HMSet(std::string key, const std::map<std::string, std::string>& hash)
{
    std::string ret_value;
    if (!Init()) {
        return ret_value;
    }
    if (hash.size() == 0) {
        return ret_value;
    }

    int argc = hash.size() * 2 + 2;
    if (argc <= 2) {
        return ret_value;
    }
    const char** argv = new const char*[argc];
    if (!argv) {
        return ret_value;
    }

    argv[0] = "HMSET";
    argv[1] = key.c_str();
    int i = 2;
    for (auto it = hash.begin(); it != hash.end(); it++) {
        argv[i++] = const_cast<char*>(it->first.c_str());
        argv[i++] = const_cast<char*>(it->second.c_str());
    }

    redisReply* reply = (redisReply*)redisCommandArgv(
        context_, argc, (const char**)argv, NULL);
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        delete[] argv;

        redisFree(context_);
        context_ = NULL;

        return ret_value;
    }

    ret_value.append(reply->str, reply->len);
    delete[] argv;
    freeReplyObject(reply);
    return ret_value;
}

bool CacheConn::HMGet(std::string key, std::list<std::string>& fields, std::list<std::string>& ret_value)
{
    if (!Init()) {
        return false;
    }
    if (fields.size() == 0) {
        return false;
    }

    int argc = fields.size() + 2;
    if (argc <= 2) {
        return false;
    }
    char** argv = new char*[argc];
    if (!argv) {
        return false;
    }

    argv[0] = "HMGET";
    argv[1] = const_cast<char*>(key.c_str());
    int i = 2;
    for (auto it = fields.begin(); it != fields.end(); it++) {
        argv[i++] = const_cast<char*>(it->c_str());
    }
    redisReply* reply = (redisReply*)redisCommandArgv(
        context_, argc, (const char**)argv, NULL);
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        delete[] argv;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; i++) {
            redisReply* value_reply = reply->element[i];
            if (value_reply->type == REDIS_REPLY_STRING) {
                std::string value(value_reply->str, value_reply->len);
                ret_value.push_back(value);
            } else if (value_reply->type == REDIS_REPLY_NIL) {
                ret_value.push_back("");
            } else {
                LOG_ERROR << "HMGET: " << key << " Error: " << value_reply->str << ", " << context_->errstr
                          << ", reply->integer: " << value_reply->integer << ", type: " << value_reply->type;
                ret_value.push_back("");
            }
        }
    }
    delete[] argv;
    freeReplyObject(reply);
    return true;
}
   
bool CacheConn::Incr(std::string key, int64_t& value)
{
    value = 0;
    if (!Init()) {
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "INCR %s", key.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    value = reply->integer;
    freeReplyObject(reply);
    return true;
}

bool CacheConn::Decr(std::string key, int64_t& value)
{
    value = 0;
    if (!Init()) {
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "DECR %s", key.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    value = reply->integer;
    freeReplyObject(reply);
    return true;
}

long CacheConn::LPush(std::string key, std::string value)
{
    if (!Init()) {
        return -1;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "LPUSH %s %s",
                                                   key.c_str(), value.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }

    long ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

long CacheConn::RPush(std::string key, std::string value)
{
    if (!Init()) {
        return -1;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "RPUSH %s %s",
                                                   key.c_str(), value.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }

    long ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

long CacheConn::LLen(std::string key)
{
    if (!Init()) {
        return -1;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "LLEN %s", key.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }

    long ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

bool CacheConn::LRange(std::string key, long start, long end, std::list<std::string> &ret_value)
{
    if (!Init()) {
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "LRANGE %s %d %d", 
                        key.c_str(), start, end);
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }

    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; i++) {
            redisReply* value_reply = reply->element[i];
            if (value_reply->type == REDIS_REPLY_STRING) {
                std::string value(value_reply->str, value_reply->len);
                ret_value.push_back(value);
            } else if (value_reply->type == REDIS_REPLY_NIL) {
                ret_value.push_back("");
            } else {
                LOG_ERROR << "LRANGE: " << key << " Error: " << value_reply->str << ", " << context_->errstr
                          << ", reply->integer: " << value_reply->integer << ", type: " << value_reply->type;
                ret_value.push_back("");
            }
        }
    }

    freeReplyObject(reply);
    return true;
}

bool CacheConn::ZSetExist(std::string key, std::string member)
{
    if (!Init()) {
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "ZLEXCOUNT %s [%s [%s", key.c_str(), member.c_str(), member.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    if (reply->type != REDIS_REPLY_INTEGER) {
        LOG_ERROR << "ZLEXCOUNT: " << key << ", member: " << member << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return false;
    }
    bool ret_value = (reply->integer > 0);
    freeReplyObject(reply);
    return ret_value;
}

int CacheConn::ZSetAdd(std::string key, long score, std::string member)
{
    if (!Init()) {
        return -1;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "ZADD %s %ld %s", key.c_str(), score, member.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }
    if (reply->type != REDIS_REPLY_INTEGER) {
        LOG_ERROR << "ZADD: " << key << ", member: " << member << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return -1;
    }
    //执行命令, reply->integer成功返回1，reply->integer返回0表示member已存在，仅更新score
    int ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

int CacheConn::ZSetRem(std::string key, std::string member)
{
    if (!Init()) {
        return -1;
    }
    redisReply* reply = (redisReply*)redisCommand(context_, "ZREM %s %s", key.c_str(), member.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }
    if (reply->type != REDIS_REPLY_INTEGER) {
        LOG_ERROR << "ZREM: " << key << ", member: " << member << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return -1;
    }
    int ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;

}

int CacheConn::ZSetIncr(std::string key, std::string member)
{
    if (!Init()) {
        return -1;
    }
    redisReply* reply = (redisReply*)redisCommand(context_, "ZINCRBY %s 1 %s", key.c_str(), member.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }
    if (reply->type != REDIS_REPLY_STRING) {
        LOG_ERROR << "ZINCRBY: " << key << ", member: " << member << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return -1;
    }
    if (strcmp(reply->str, "OK") != 0) {
        LOG_ERROR << "ZINCRBY: " << key << ", member: " << member << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return -1;
    }
    int ret_value = 0;
    freeReplyObject(reply);
    return ret_value;
}

int CacheConn::ZSetCard(std::string key)
{
    if (!Init()) {
        return -1;
    }
    redisReply* reply = (redisReply*)redisCommand(context_, "ZCARD %s", key.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }
    if (reply->type != REDIS_REPLY_INTEGER) {
        LOG_ERROR << "ZCARD: " << key << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return -1;
    }
    int ret_value = reply->integer;
    freeReplyObject(reply);
    return ret_value;
}

int CacheConn::ZSetRevRange(std::string key, int start, int end, RVALUES values, int& get_num)
{
    if (!Init()) {
        return -1;
    }
    int count = end - start + 1;
    redisReply* reply = (redisReply*)redisCommand(context_, "ZREVRANGE %s %d %d", key.c_str(), start, end);
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }
    if (reply->type != REDIS_REPLY_ARRAY) {
        LOG_ERROR << "ZREVRANGE: " << key << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return -1;
    }
    get_num = reply->elements;
    if (get_num > count) {
        get_num = count;
    }
    for (size_t i = 0; i < get_num; i++) {
        redisReply* value_reply = reply->element[i];
        if (value_reply->type == REDIS_REPLY_STRING) {
            strncpy(values[i], reply->element[i]->str, VALUES_ID_SIZE - 1);
            values[i][VALUES_ID_SIZE - 1] = '\0';
        } else {
            values[i][0] = '\0';
        }
    }
    freeReplyObject(reply);
    return 0;

}

int CacheConn::ZSetGetScore(std::string key, std::string member)
{
    if (!Init()) {
        return -1;
    }

    int score = 0;
    redisReply* reply = (redisReply*)redisCommand(context_, "ZSCORE %s %s", key.c_str(), member.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return -1;
    }
    if (reply->type == REDIS_REPLY_STRING) {
        score = atoi(reply->str);
    } else if (reply->type == REDIS_REPLY_NIL) {
        score = 0;
    } else {
        LOG_ERROR << "ZSCORE: " << key << ", member: " << member << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return -1;
    }
    freeReplyObject(reply);
    return score;
}

bool CacheConn::GetXRevRange(const std::string& key, const std::string& start, 
    const std::string& end, int count, 
    std::vector<std::pair<std::string, std::string>>& msgs)
{
    if (!Init()) {
        return false;
    }
    std::string command = "XREVRANGE " + key + " " + start + " " + end;
    if (count > 0) {
        command += " COUNT " + std::to_string(count);
    }
    LOG_DEBUG << "command: " << command;
    redisReply* reply = (redisReply*)redisCommand(context_, command.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    if (reply->type != REDIS_REPLY_ARRAY) {
        LOG_ERROR << "XREVRANGE: " << key << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return false;
    }
    for (size_t i = 0; i < reply->elements; i++) {
        redisReply* entry_reply = reply->element[i];
        if (entry_reply->type != REDIS_REPLY_ARRAY || entry_reply->elements != 2) {
            LOG_ERROR << "XREVRANGE: " << key << " entry_reply format error";
            continue;
        }
        /*第一个元素是消息ID*/
        std::string msg_id = entry_reply->element[0]->str;
        /*第二个元素是字段-值对*/
        redisReply* field_values_reply = entry_reply->element[1];
        if (field_values_reply->type != REDIS_REPLY_ARRAY || field_values_reply->elements % 2 != 0) {
            LOG_ERROR << "XREVRANGE: " << key << " field_values_reply format error";
            continue;
        }
        for (size_t j = 0; j < field_values_reply->elements; j += 2) {
            redisReply* field_reply = field_values_reply->element[j];
            redisReply* value_reply = field_values_reply->element[j + 1];
            if (field_reply->type != REDIS_REPLY_STRING || value_reply->type != REDIS_REPLY_STRING) {
                LOG_ERROR << "XREVRANGE: " << key << " field or value format error";
                continue;
            }
            std::string field = field_reply->str;
            std::string value = value_reply->str;
            msgs.push_back(std::make_pair(field, value));
        }
        LOG_DEBUG << "XREVRANGE: " << key << " msg_id: " << msg_id << ", field_values size: " << msgs.size();
    }
    freeReplyObject(reply);
    return true;

}

bool CacheConn::XAdd(const std::string& key, std::string& id, const std::vector<std::pair<std::string, std::string>>& field_values)
{
    if (!Init()) {
        return false;
    }
    std::string command = "XADD " + key + " " + id + " ";
    for (const auto& fv : field_values) {
        command += fv.first + " " + fv.second + " ";
    }
    LOG_DEBUG << "command: " << command;
    redisReply* reply = (redisReply*)redisCommand(context_, command.c_str());
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    if (reply->type != REDIS_REPLY_STRING) {
        LOG_ERROR << "XADD: " << key << " Error: " << reply->str << ", " << context_->errstr
                  << ", reply->integer: " << reply->integer << ", type: " << reply->type;
        freeReplyObject(reply);
        return false;
    }
    id = reply->str;
    LOG_DEBUG << "XADD: " << key << " id: " << id;
    freeReplyObject(reply);
    return true;
}

bool CacheConn::FlushDb()
{
    bool ret = false;
    if (!Init()) {
        return false;
    }

    redisReply* reply = (redisReply*)redisCommand(context_, "FLUSHDB");
    if (!reply) {
        LOG_ERROR << "redisCommand failed: " << context_->errstr;
        redisFree(context_);
        context_ = NULL;
        return false;
    }
    if (reply->type == REDIS_REPLY_STRING &&
        strncmp(reply->str, "OK", 2) == 0) {
        ret = true;
    }

    freeReplyObject(reply);

    return ret;

}

CachePool::CachePool(const char* pool_name, const char* server_ip,
                     int server_port, int db_index,
                     const char* password, int max_conn_cnt) 
{
    pool_name_ = pool_name;
    server_ip_ = server_ip;
    server_port_ = server_port;
    db_index_ = db_index;
    password_ = password;
    max_conn_cnt_ = max_conn_cnt;
    cur_conn_cnt_ = MIN_CACHE_CONN_CNT; // 初始连接数为最大连接数的一半
    abort_request_ = false;
}

CachePool::~CachePool()
{
    /*加锁避免析构过程中其他线程操作共享数据*/
    {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_request_ = true;
        cond_var_.notify_all(); // 通知所有在等待的
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::list<CacheConn*>::iterator it = free_list_.begin(); it != free_list_.end(); it++) {
            CacheConn* pConn = *it;
            delete pConn;
        }
    }
    free_list_.clear();
    cur_conn_cnt_ = 0;

}

bool CachePool::Init()
{
    for(int i = 0; i < cur_conn_cnt_; i++) {
        CacheConn* pConn = new CacheConn(server_ip_.c_str(), server_port_, db_index_,
                                        password_.c_str(), pool_name_.c_str());
        if (!pConn->Init()) {
            LOG_ERROR << "CachePool::Init, init conn failed, pool_name_: " << pool_name_.c_str()
                      << ", server_ip_: " << server_ip_.c_str() << ", server_port_: " << server_port_;
            delete pConn;
            return false;
        }

        free_list_.push_back(pConn);
    }
    LOG_INFO << "CachePool: " << pool_name_.c_str() << ", list size: " << free_list_.size();
    return true;
}

CacheConn* CachePool::GetCacheConn(const int timeout_ms)
{
    LOG_INFO << "CachePool::GetCacheConn, free_list size: " << free_list_.size()
              << ", cur_conn_cnt_: " << cur_conn_cnt_;
    std::unique_lock<std::mutex> lock(mutex_);
    if (abort_request_) {
        LOG_INFO << "CachePool::GetCacheConn abort_request_ is true";
        return nullptr;
    }

    if (free_list_.empty()) {
        if (cur_conn_cnt_ >= max_conn_cnt_) // 等待的逻辑
        {
            // 如果已经到达了，看看是否需要超时等待
            if (timeout_ms <= 0) // 死等，直到有连接可以用 或者 连接池要退出
            {
                LOG_INFO << "wait ms: " << timeout_ms;
                cond_var_.wait(lock, [this] {
                    // 当前连接数量小于最大连接数量 或者请求释放连接池时退出
                    return (!free_list_.empty()) | abort_request_;
                });
            } else {
                // return如果返回 false，继续wait(或者超时),
                // 如果返回true退出wait 1.m_free_list不为空 2.超时退出
                // 3. m_abort_request被置为true，要释放整个连接池
                cond_var_.wait_for(
                    lock, std::chrono::milliseconds(timeout_ms),
                    [this] { return (!free_list_.empty()) | abort_request_; });
                // 带超时功能时还要判断是否为空
                if (free_list_.empty()) // 如果连接池还是没有空闲则退出
                {
                    LOG_INFO << "CachePool::GetCacheConn wait timeout";
                    return nullptr;
                }
            }

            if (abort_request_) {
                LOG_INFO << "CachePool::GetCacheConn abort_request_ is true";
                return nullptr;
            }
        } else // 还没有到最大连接则创建连接
        {
            CacheConn* pConn = new CacheConn(server_ip_.c_str(), server_port_, db_index_,
                                            password_.c_str(), pool_name_.c_str());
            if (pConn->Init()) {
                delete pConn;
                return NULL;
            }
            cur_conn_cnt_++;
            LOG_INFO << "create new conn, curr_conn_cnt_: " << cur_conn_cnt_;
            free_list_.push_back(pConn);
        }
    }
    CacheConn* pConn = free_list_.front();
    free_list_.pop_front();
    return pConn;
}

void CachePool::RelCacheConn(CacheConn *p_cache_conn) {
    std::lock_guard<std::mutex> lock(mutex_);

    list<CacheConn *>::iterator it = free_list_.begin();
    for (; it != free_list_.end(); it++) {
        if (*it == p_cache_conn) {
            LOG_WARN << "RelDBConn warning, the conn has in free_list";
            break;
        }
    }

    if (it == free_list_.end()) {
        // m_used_list.remove(pConn);
        free_list_.push_back(p_cache_conn);
        cond_var_.notify_one(); // 通知取队列
    } else {
        LOG_ERROR << "RelDBConn failed"; // 不再次回收连接
    }
}

CacheManager::CacheManager() 
{

}

CacheManager::~CacheManager()
{

}

void CacheManager::SetConfPath(const char* conf_path)
{
    conf_path_ = conf_path;
}

CacheManager* CacheManager::getInstance()
{
    static CacheManager instance;
    static bool inited = instance.Init();
    return inited ? &instance : nullptr;
}

bool CacheManager::Init()
{
    CConfigFileReader config_file(conf_path_.c_str());
    char* cache_instances = config_file.GetConfigValue("CacheInstances");
    if (!cache_instances) {
        LOG_ERROR << "not configure CacheIntance";
        return false;
    }

    char host[64];
    char port[64];
    char db[64];
    char maxconncnt[64];
    CStrExplode instances_name(cache_instances, ',');
    for (uint32_t i = 0; i < instances_name.GetItemCnt(); i++) {
        char* pool_name = instances_name.GetItem(i);
        snprintf(host, 64, "%s_host", pool_name);
        snprintf(port, 64, "%s_port", pool_name);
        snprintf(db, 64, "%s_db", pool_name);
        snprintf(maxconncnt, 64, "%s_maxconncnt", pool_name);

        char* cache_host = config_file.GetConfigValue(host);
        char* str_cache_port = config_file.GetConfigValue(port);
        char* str_cache_db = config_file.GetConfigValue(db);
        char* str_max_conn_cnt = config_file.GetConfigValue(maxconncnt);
        if (!cache_host || !str_cache_port || !str_cache_db || !str_max_conn_cnt) {
            if(!cache_host)
                LOG_ERROR << "not configure cache instance: " <<  pool_name << ", cache_host is null";
            if(!str_cache_port)
                LOG_ERROR << "not configure cache instance: " << pool_name << ", str_cache_port is null";
            if(!str_cache_db)
                LOG_ERROR << "not configure cache instance: " << pool_name << ", str_cache_db is null";
            if(!str_max_conn_cnt)
                LOG_ERROR << "not configure cache instance: " << pool_name << ", str_max_conn_cnt is null";
            return false;
        }

        int cache_port = atoi(str_cache_port);
        int cache_db = atoi(str_cache_db);
        int max_conn_cnt = atoi(str_max_conn_cnt);

        CachePool* pCachePool = new CachePool(pool_name, cache_host, cache_port, atoi(str_cache_db), "", atoi(str_max_conn_cnt));
        if (!pCachePool) {
            LOG_ERROR << "new CachePool failed";
            return false;
        }
        if (!pCachePool->Init()) {
            LOG_ERROR << "init CachePool failed";
            delete pCachePool;
            return false;
        }
        cache_pool_map_.insert(std::make_pair(pool_name, pCachePool));
    }

    return true;

}

CacheConn *CacheManager::GetCacheConn(const char *pool_name) {
    LOG_INFO << "GetCacheConn, pool_name: " << pool_name;
    map<string, CachePool *>::iterator it = cache_pool_map_.find(pool_name);
    if (it != cache_pool_map_.end()) {
        return it->second->GetCacheConn();
    } else {
        LOG_ERROR << "not found cache pool: " << pool_name;
        return nullptr;
    }
}

void CacheManager::RelCacheConn(CacheConn *cache_conn) {
    if (!cache_conn) {
        return;
    }

    map<string, CachePool *>::iterator it =
        cache_pool_map_.find(cache_conn->GetPoolName());
    if (it != cache_pool_map_.end()) {
        return it->second->RelCacheConn(cache_conn);
    }
}

std::string CacheManager::conf_path_;