#ifndef __CHATROOM_APPLICATION_API_API_ROOM_H__
#define __CHATROOM_APPLICATION_API_API_ROOM_H__

#include <string>
#include <vector>
#include "api_types.h"

bool api_create_room(const std::string& room_id, const std::string& room_name, 
    int creator_id, std::string& error_msg);

bool api_get_room_info(std::string& room_id, std::string& room_name,
    int& creator_id, std::string& create_time, std::string& update_time, 
    std::string& error_msg);

bool api_get_all_rooms(std::vector<Room>& rooms, std::string& error_msg, 
    const std::string& ordered_by = "create_time DESC");

#endif