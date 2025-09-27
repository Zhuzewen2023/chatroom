#ifndef __CHAT_ROOM_API_API_TYPES_H__
#define __CHAT_ROOM_API_API_TYPES_H__

#include "api_common.h"
#include <string>
#include <vector>

typedef struct _room
{
    std::string room_id; //房间id
    std::string room_name;//房间名
    int creator_id;
    std::string create_time;
    std::string update_time;
    std::string history_last_message_id; //这个房间翻阅历史消息时的位置
}Room;

// 定义 Me 结构体
struct User {
    int id;                        // 用户 ID
    std::string username;          // 用户名
};

// A chat message
struct Message
{
    // Message ID
    std::string id;

    // The actual content of the message
    std::string content;

    // UTC timestamp when the server received the message
    uint64_t timestamp; //直接存储秒的单位

    // ID of the user that sent the message
    std::int64_t user_id{};
    std::string username;
};


struct MessageBatch
{
    // The messages in the batch
    std::vector<Message> messages;

    // true if there are more messages that could be loaded
    bool has_more{};
};
 

#endif