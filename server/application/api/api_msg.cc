#include "api_msg.h"
#include "cache_pool.h"
#include <json/json.h>

constexpr int MESSAGE_BATCH_SIZE = 20;

int api_get_room_history(Room& room, MessageBatch& msg_batch)
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
    bool res = cache_conn->GetXRevRange(room.room_id, stream_ref, "-", MESSAGE_BATCH_SIZE, msgs);
    if (res) {
        for (int i = 0; i < msgs.size(); i++) {
            Message msg;
            msg.id = msgs[i].first;
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
            if (root["user_id"].isNull()) {
                LOG_ERROR << "user_id is null";
                return -1;
            }
            msg.user_id = root["user_id"].asInt64();
            if (root["timestamp"].isNull()) {
                LOG_ERROR << "timestamp is null";
                return -1;
            }
            msg.timestamp = root["timestamp"].asUInt64();
            msg_batch.messages.push_back(msg);
        }
        if (msgs.size() < MESSAGE_BATCH_SIZE) {
            msg_batch.has_more = false;
        } else {
            msg_batch.has_more = true;
        }
        /*XADD 0001 * payload "{\"content\": \"test\", \"timestamp\": 1737193440751, \"user_id\": 1}"*/
        return 0;
    } else {
        LOG_ERROR << "GetXrevrange failed: room_id=" << room.room_id << ", stream_ref=" << stream_ref << ", end_ref= \"-\", message_batch_size=" << MESSAGE_BATCH_SIZE;
        return -1;
    }
}