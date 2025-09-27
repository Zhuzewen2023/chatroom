
#ifndef __PUB_SUB_SERVICE_H__
#define __PUB_SUB_SERVICE_H__
#include "api_types.h"
#include <vector>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>

//房间管理
class RoomTopic 
{
public:
    RoomTopic(const std::string &room_id, const std::string &room_topic, uint32_t creator_id) {
        room_id_ = room_id;
        room_topic_ = room_topic;
        creator_id_ = creator_id;
    }
    ~RoomTopic() {
        user_ids_.clear();
    }


    void AddSubscriber(uint32_t userid) {
        user_ids_.insert(userid);
    }
    void DeleteSubscriber(uint32_t userid) {
        user_ids_.erase(userid);
    }
    std::unordered_set<uint32_t> &getSubscribers() {
        return user_ids_;
    }
 private:
    std::string room_id_;
    std::string room_topic_;
    int creator_id_;
    std::unordered_set<uint32_t> user_ids_;
};

using RoomTopicPtr = std::shared_ptr<RoomTopic>;

using PubSubCallback = std::function<void(const std::unordered_set<uint32_t> &user_ids)>;
class PubSubService
{
public:
    //单例模式
    static PubSubService &GetInstance() {
        static PubSubService instance;
        return instance;
    }
    PubSubService() {}
    ~PubSubService(){}
    bool AddRoomTopic(const std::string &room_id, const std::string &room_topic, int creator_id) {
        std::lock_guard<std::mutex> lck(room_topic_map_mutex_);
        LOG_INFO << "AddRoomTopic: " << room_id << ", room_topic: " << room_topic << ", creator_id: " << creator_id;
        if (room_topic_map_.find(room_id) != room_topic_map_.end()) {
            LOG_WARN << "room_id: " << room_id << " already exists";
            return false;
        }
        RoomTopicPtr room_topic_ptr = std::make_shared<RoomTopic>(room_id, room_topic, creator_id);
        room_topic_map_[room_id] = room_topic_ptr;
        return true;
    }
    void DeleteRoomTopic(const std::string &room_id) {
        std::lock_guard<std::mutex> lck(room_topic_map_mutex_);
        if (room_topic_map_.find(room_id) != room_topic_map_.end()) {
            return;
        }
        room_topic_map_.erase(room_id);
    }
    bool AddSubscriber(const std::string &room_id, uint32_t userid) {
        LOG_DEBUG << "AddSubscriber: " << room_id << ", userid: " << userid;

        std::lock_guard<std::mutex> lck(room_topic_map_mutex_);
        if (room_topic_map_.find(room_id) == room_topic_map_.end()) {
            LOG_WARN << "can't find room_id: " << room_id;
            return false;
        }
        room_topic_map_[room_id]->AddSubscriber(userid);
        return true;
    }
    void DeleteSubscriber(const std::string &room_id, uint32_t userid) {
        std::lock_guard<std::mutex> lck(room_topic_map_mutex_);
        if (room_topic_map_.find(room_id) == room_topic_map_.end()) {
            return;
        }
        room_topic_map_[room_id]->DeleteSubscriber(userid);
    }
    void PublishMessage(const std::string &room_id,  PubSubCallback callback) {
        std::unordered_set<uint32_t> user_ids;
        {
            std::lock_guard<std::mutex> lck(room_topic_map_mutex_);
            if (room_topic_map_.find(room_id) == room_topic_map_.end()) {
                return;
            }
            user_ids = room_topic_map_[room_id]->getSubscribers();
        }
        
        callback(user_ids);   //这里不能有太多耗时的工作
    }
    static std::vector<Room> &GetRoomList();  //获取当前的房间列表
    static int AddRoom(const Room &room);
private:
    std::unordered_map<std::string, RoomTopicPtr> room_topic_map_;
    std::mutex room_topic_map_mutex_;
};

//获取固定的房间


#endif