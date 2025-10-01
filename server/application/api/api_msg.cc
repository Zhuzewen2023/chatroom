#include "api_msg.h"
#include "cache_pool.h"
#include <json/json.h>

// constexpr int MESSAGE_BATCH_SIZE = 20;

int api_get_room_history(Room& room, MessageBatch& msg_batch, const int msg_count)
{
    /*
    XREVRANGE mystream + - COUNT 5
    */
    CacheManager* cache_manager = CacheManager::getInstance();
    CacheConn* cache_conn = cache_manager->GetCacheConn("msg");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);
    std::string stream_ref = "+";
    if (!room.history_last_message_id.empty()) {
        stream_ref = "(" + room.history_last_message_id;
    }
    LOG_INFO << "stream_ref: " << stream_ref;
    //消息id - 消息本身
    std::vector<std::pair<std::string, std::string>> msgs;
    bool res = cache_conn->GetXRevRange(room.room_id, stream_ref, "-", msg_count, msgs);
    if (res) {
        for (int i = 0; i < msgs.size(); i++) {
            // 新增日志：打印原始的first和second
            LOG_INFO << "msgs[" << i << "].first=" << msgs[i].first 
             << ", msgs[" << i << "].second=" << msgs[i].second;
            Message msg;
            msg.id = msgs[i].first;
            LOG_INFO << "api_get_room_history: msg id: " << msg.id;
            room.history_last_message_id = msg.id;
            Json::Value root;
            Json::Reader json_reader;
            //{"content": "", "timestamp": "", "user_id": }
            res = json_reader.parse(msgs[i].second, root);
            if (!res) {
                LOG_ERROR << "parse redis msg failed";
                return -1;
            }
            if (root["content"].isNull()) {
                LOG_ERROR << "content null";
                return -1;
            }
            msg.content = root["content"].asString();
            LOG_INFO << "api_get_room_history: msg content: " << msg.content;
            if (root["user_id"].isNull()) {
                LOG_ERROR << "user_id is null";
                return -1;
            }
            msg.user_id = root["user_id"].asInt64();
            LOG_INFO << "api_get_room_history: msg user id: " << msg.user_id;
            if (root["username"].isNull()) {
                LOG_ERROR << "username is null";
                return -1;
            }
            msg.username = root["username"].asString();
            
            if (root["timestamp"].isNull()) {
                LOG_ERROR << "timestamp is null";
                return -1;
            }
            msg.timestamp = root["timestamp"].asUInt64();
            LOG_INFO << "api_get_room_history: msg timestamp: " << msg.timestamp;
            msg_batch.messages.push_back(msg);
        }
        if (msgs.size() < msg_count) {
            msg_batch.has_more = false;
        } else {
            msg_batch.has_more = true;
        }
        /*XADD 0001 * payload "{\"content\": \"test\", \"timestamp\": 1737193440751, \"user_id\": 1}"*/
        return 0;
    } else {
        LOG_ERROR << "GetXrevrange failed: room_id=" << room.room_id << ", stream_ref=" << stream_ref << ", end_ref= \"-\", message_batch_size=" << msg_count;
        return -1;
    }
}

std::string rtrim(const std::string& s)
{
    size_t end = s.find_last_not_of(" \t\n\r");
    return (end == std::string::npos)? "": s.substr(0, end + 1);
}

std::string serialize_message_to_json(const Message msg)
{
    Json::Value root;
    root["content"] = msg.content;
    root["timestamp"] = static_cast<Json::Int64>(msg.timestamp);
    root["user_id"] = msg.user_id;
    root["username"] = msg.username;
    Json::FastWriter fastwriter;
    std::string json_str = fastwriter.write(root);
    return rtrim(json_str);
    // return root.toStyledString();
}

int api_store_message(std::string& room_id, std::vector<Message>& msgs)
{
    CacheManager* cache_manager = CacheManager::getInstance();
    CacheConn* cache_conn = cache_manager->GetCacheConn("msg");
    AUTO_REL_CACHECONN(cache_manager, cache_conn);

    for (int i = 0; i < msgs.size(); i++) {
        std::string serialized_msg = serialize_message_to_json(msgs[i]);
        std::vector<std::pair<std::string, std::string>> field_value_pairs;
        field_value_pairs.push_back(std::make_pair("payload", serialized_msg));
        LOG_INFO << "serialized msg: " << serialized_msg;
        std::string id = "*";
        bool ret = cache_conn->XAdd(room_id, id, field_value_pairs);
        if (!ret) {
            LOG_ERROR << "XAdd failed";
            LOG_ERROR << "room_id: " << room_id << ", msg id: " << id << ", json_msg: " << serialized_msg;
            return -1;
        }
        LOG_INFO << "room_id: " << room_id << ", msg id: " << id << ", json_msg: " << serialized_msg;
        msgs[i].id = id;
    }
    return 0;
}