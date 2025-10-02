#include "api_room.h"
#include "db_pool.h"
#include <sstream>

bool api_create_room(const std::string& room_id, const std::string& room_name, 
    int creator_id, std::string& error_msg)
{
    CDBManager* db_manager = CDBManager::getInstance();
    CDBConn* db_conn = db_manager->GetDBConn("chatroom_master");
    if (!db_conn) {
        error_msg = "api_create_room, cannot get db connection [chatroom_master]";
        return false;
    }
    AUTO_REL_DBCONN(db_manager, db_conn);
    std::stringstream ss;
    ss << "INSERT INTO room_info (room_id, room_name, creator_id) VALUES ('"
        << room_id << "', '" << room_name << "', '" << creator_id << "')";
    if (!db_conn->ExecuteUpdate(ss.str().c_str())) {
        LOG_INFO << "api create room, db_conn->ExecuteUpdate failed";
        error_msg = "api create room, db_conn->ExecuteUpdate failed";
        return false;
    }
    return true;
}

bool api_get_room_info(std::string& room_id, std::string& room_name,
    int& creator_id, std::string& create_time, std::string& update_time, 
    std::string& error_msg)
{
    CDBManager* db_manager = CDBManager::getInstance();
    CDBConn* db_conn = db_manager->GetDBConn("chatroom_slave");
    if (!db_conn) {
        error_msg = "api_get_room_info, cannot get db connection [chatroom_slave]";
        return false;
    }
    AUTO_REL_DBCONN(db_manager, db_conn);
    std::stringstream ss;
    ss << "SELECT room_id, room_name, creator_id, create_time, update_time FROM room_info WHERE room_id='" << room_id << "'";
    CDBResultSet* result_set = db_conn->ExecuteQuery(ss.str().c_str());
    if (!result_set) {
        LOG_ERROR << "api_get_room_info db_conn->ExecuteQuery failed";
        error_msg = "api_get_room_info db_conn->ExecuteQuery failed";
        return false;
    }

    if (result_set->Next()) {
        
        room_id = result_set->GetString("room_id");
        room_name = result_set->GetString("room_name");
        creator_id = result_set->GetInt("creator_id");
        create_time = result_set->GetString("create_time");
        update_time = result_set->GetString("update_time");
        LOG_INFO << "api_get_room_info room_id = " << room_id << ", room_name = " << room_name << ", creator_id = " << creator_id << ", create_time = " << create_time << ", update_time = " << update_time;
        delete result_set;

    } else {
        delete result_set;
        LOG_ERROR << "api_get_room_info failed, no room info";
        error_msg = "api_get_room_info failed, no room info";
        return false;
    }
    return true;
}

bool api_get_all_rooms(std::vector<Room>& rooms, std::string& error_msg, 
    const std::string& ordered_by)
{
    CDBManager* db_manager = CDBManager::getInstance();
    CDBConn* db_conn = db_manager->GetDBConn("chatroom_slave");
    if (!db_conn) {
        error_msg = "api_get_all_rooms, cannot get db connection [chatroom_slave]";
        return false;
    }
    AUTO_REL_DBCONN(db_manager, db_conn);

    std::stringstream ss;
    ss << "SELECT room_id, room_name, creator_id, create_time, update_time FROM room_info ORDER BY " << ordered_by;

    CDBResultSet* result_set = db_conn->ExecuteQuery(ss.str().c_str());

    if (!result_set) {
        LOG_ERROR << "api_get_all_rooms db_conn->ExecuteQuery failed";
        error_msg = "api_get_all_rooms db_conn->ExecuteQuery failed";
        return false;
    }

    while (result_set->Next()) {
        std::string room_id = result_set->GetString("room_id");
        std::string room_name = result_set->GetString("room_name");
        int creator_id = result_set->GetInt("creator_id");
        std::string create_time = result_set->GetString("create_time");
        std::string update_time = result_set->GetString("update_time");
        LOG_INFO << "api_get_all_rooms room_id = " << room_id << ", room_name = " << room_name << ", creator_id = " << creator_id << ", create_time = " << create_time << ", update_time = " << update_time;
        Room room;
        room.room_id = room_id;
        room.room_name = room_name;
        room.creator_id = creator_id;
        room.create_time = create_time;
        room.update_time = update_time;
        rooms.push_back(room);

    }
    delete result_set;    
    return true;
}