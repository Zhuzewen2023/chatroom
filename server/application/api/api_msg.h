#ifndef __CHATROOM_APPLICATION_API_API_MSG_H__
#define __CHATROOM_APPLICATION_API_API_MSG_H__

#include "api_types.h"

constexpr int K_MESSAGE_BATCH_SIZE = 5;

int api_get_room_history(Room& room, MessageBatch& msgs, const int msg_count = K_MESSAGE_BATCH_SIZE);
int api_store_message(std::string& room_id, std::vector<Message>& msgs);

#endif