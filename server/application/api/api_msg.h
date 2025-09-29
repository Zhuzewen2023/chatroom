#ifndef __CHATROOM_APPLICATION_API_API_MSG_H__
#define __CHATROOM_APPLICATION_API_API_MSG_H__

#include "api_types.h"

int api_get_room_history(Room& room, MessageBatch& msgs);

#endif