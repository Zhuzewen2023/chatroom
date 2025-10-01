#include "pub_sub_service.h"

/*
typedef struct _room
{
    string room_id; //房间id
    string room_name;//房间名
    int creator_id;
    string create_time;
    string update_time;
    string history_last_message_id; //这个房间翻阅历史消息时的位置
}Room;
*/

static std::vector<Room> s_room_list = {
    {"0001", "Linux", 1, "", "", ""},
    {"0002", "Python", 2, "", "", ""},
    {"0003", "C++", 3, "", "", ""},
    {"0004", "C", 4, "", "", ""},
    {"0005", "Golang", 5, "", "", ""}
};

static std::mutex s_room_list_mutex_;

std::vector<Room>& PubSubService::GetRoomList()
{
    std::lock_guard<std::mutex> lock(s_room_list_mutex_);
    return s_room_list;
}

#if 0
// static std::vector<Room> s_room_list = {
//     {"0001", "程序员老廖2", 1, "", "", ""},
//     {"0002", "码农mark2", 2, "", "", ""},
//     {"0003", "程序员yt2", 3, "", "", ""},
//     {"0004", "老周讲golang2", 4, "", "", ""},
//     {"0005", "绝顶哥编程vico2", 5, "", "", ""}
// };

static std::vector<Room> s_room_list = {
    {"0001", "Linux服务器课程", 1, "", "", ""},
    {"0002", "音视频课程", 2, "", "", ""},
    {"0003", "QT课程", 3, "", "", ""},
    {"0004", "Golang课程", 4, "", "", ""},
    {"0005", "游戏课程", 5, "", "", ""},
    {"0006", "Linux内核课程", 6, "", "", ""},
    {"0007", "DPDK课程", 7, "", "", ""},
    {"0008", "存储课程", 8, "", "", ""}
};

static std::mutex s_metux_room_list;

 std::vector<Room> &PubSubService::GetRoomList() {
    //加锁
    std::lock_guard<std::mutex> lock(s_metux_room_list);
    return s_room_list;
 }

int PubSubService::AddRoom(const Room &room) {
    std::lock_guard<std::mutex> lock(s_metux_room_list);
    // 判断是否存在
    for(const auto &r : s_room_list) {
        if(r.room_id == room.room_id) {
            LOG_WARN << "room already exists";
            return -1;
        }
    }
    s_room_list.push_back(room);
     
    return 0;
 }
 #endif
 