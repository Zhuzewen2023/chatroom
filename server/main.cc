#include "muduo/net/TcpServer.h"
#include  "muduo/net/TcpConnection.h"
#include "muduo/base/ThreadPool.h"

#include "muduo/net/EventLoop.h"  //EventLoop
#include "muduo/base/Logging.h" // Logger日志头文件
#include "config_file_reader.h"
#include "db_pool.h"
#include "cache_pool.h"
#include "pub_sub_service.h"
#include "websocket_conn.h"
#include "http_handler.h"

#include <iostream>
#include <signal.h>
#include <thread>

using namespace muduo;
using namespace muduo::net;

std::map<uint32_t, HttpHandlerPtr> s_http_handler_map;

class HttpServer
{
public:
    //构造函数 loop主线程的EventLoop， addr封装ip，port, name服务名字，num_event_loops多少个subReactor
    HttpServer(EventLoop *loop, const InetAddress &addr, const std::string &name, int num_event_loops, int num_threads)
    :loop_(loop)
    , server_(loop, addr,name)
    , num_threads_(num_threads)
    {
        server_.setConnectionCallback(std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
        server_.setMessageCallback(
            std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        server_.setWriteCompleteCallback(std::bind(&HttpServer::onWriteComplete, this, std::placeholders::_1));
   
        server_.setThreadNum(num_event_loops);
    }
    void start() {
        if(num_threads_ != 0) {
            // 初始化线程池，比如设置4个线程
            CWebSocketConn::InitThreadPool(num_threads_);  // 添加这行，在服务启动时初始化线程池
            // thread_pool_.start(num_threads_);
        }
            
        server_.start();
    }
private:
    void onConnection(const TcpConnectionPtr &conn)  {
        if (conn->connected())
        {
            uint32_t uuid = conn_uuid_generator_++;
            LOG_INFO << "uuid: " << uuid << ", onConnection new conn" << conn.get(); //conn.get()返回的是TcpConnection的裸指针
            conn->setContext(uuid);
            HttpHandlerPtr http_conn = std::make_shared<HttpHandler>(conn);
         
            std::lock_guard<std::mutex> ulock(mtx_); //自动释放
            s_http_handler_map.insert({ uuid, http_conn});
         
        } else {
            uint32_t uuid = std::any_cast<uint32_t>(conn->getContext());
            LOG_INFO << "uuid: " << uuid << ", onConnection dis conn" << conn.get();
            std::lock_guard<std::mutex> ulock(mtx_); //自动释放
            s_http_handler_map.erase(uuid);
        }
    }

    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time) {
        LOG_DEBUG <<  "onMessage " << conn.get();
        uint32_t uuid = std::any_cast<uint32_t>(conn->getContext());
        mtx_.lock();  
        HttpHandlerPtr &http_conn = s_http_handler_map[uuid];
        mtx_.unlock();
         //处理 相关业务
        if(num_threads_ != 0)  //开启了线程池
            //不能在这里做多线程的处理，需要解析出来完整的数据了再丢给线程池
            thread_pool_.run(std::bind(&HttpHandler::OnRead, http_conn, buf)); //给到业务线程处理
        else {  //没有开启线程池
            http_conn->OnRead(buf);  // 直接在io线程处理
        }   
       
    }

    void onWriteComplete(const TcpConnectionPtr& conn) {
        LOG_DEBUG <<  "onWriteComplete " << conn.get();
    }


    TcpServer server_;    // 每个连接的回调数据 新的连接/断开连接  收到数据  发送数据完成   
    EventLoop *loop_ = nullptr; //这个是主线程的EventLoop
    std::atomic<uint32_t> conn_uuid_generator_ = 0;  //这里是用于http请求，不会一直保持链接
    std::mutex mtx_;

    //线程池
    ThreadPool thread_pool_;
    const int num_threads_ = 0;
};

// int load_room_list() {
//     PubSubService& pubSubService = PubSubService::GetInstance();
//     string error_msg;
//     std::vector<Room> &room_list = PubSubService::GetRoomList(); //获取缺省的聊天室列表
//     //room_list写入数据库里
//     for(const auto &room : room_list)    {
//         string room_id = room.room_id;
//         std::string existing_room_name;
//         int creator_id;
//         std::string create_time, update_time;
//         if(!ApiGetRoomInfo(room_id, existing_room_name, creator_id, 
//                                 create_time, update_time, error_msg)) {
//             // 房间不存在，创建新房间
//             const int SYSTEM_USER_ID = 1;  // 默认使用ID为1的用户作为创建者
//             if (!ApiCreateRoom(room.room_id, room.room_name, SYSTEM_USER_ID, error_msg)) {
//                 LOG_ERROR << "Failed to create room " << room.room_id << ": " << error_msg;
//                 continue;
//             }
//              LOG_INFO << "Created new room: " << room.room_id << " - " << room.room_name;     
//         }
//     }
    
//     std::vector<Room> all_rooms;
//     if (!ApiGetAllRooms(all_rooms, error_msg)) {
//         LOG_ERROR << "Failed to get all rooms: " << error_msg;
//         return -1;
//     }
    
    
//     for (const auto& room : all_rooms) {
//         PubSubService::AddRoom(room);
//         pubSubService.AddRoomTopic(room.room_id, room.room_name, 1);
//         LOG_INFO << "Added room to PubSubService: " << room.room_id << " - " << room.room_name;
//     }

//     return 0;
// }

int main(int argc, char *argv[])
{
    std::cout  << argv[0] << " [conf ] "<< std::endl;
     
     // 默认情况下，往一个读端关闭的管道或socket连接中写数据将引发SIGPIPE信号。我们需要在代码中捕获并处理该信号，
    // 或者至少忽略它，因为程序接收到SIGPIPE信号的默认行为是结束进程，而我们绝对不希望因为错误的写操作而导致程序退出。
    // SIG_IGN 忽略信号的处理程序
    signal(SIGPIPE, SIG_IGN); //忽略SIGPIPE信号
    int ret = 0;

    char*   str_chat_room_conf = NULL;
    if(argc > 1) {
        str_chat_room_conf = argv[1];  // 指向配置文件路径
    } else {
        str_chat_room_conf = (char *)"chat-room.conf";
    }
    std::cout << "conf file path: " << str_chat_room_conf << std::endl;
     // 读取配置文件
    CConfigFileReader config_file(str_chat_room_conf);     //读取配置文件

     //日志设置级别
    char *str_log_level =  config_file.GetConfigValue("log_level");  
    Logger::LogLevel log_level = static_cast<Logger::LogLevel>(atoi(str_log_level));
    Logger::setLogLevel(log_level);

     // 初始化mysql、redis连接池，内部也会读取读取配置文件 chat-room.conf
    CacheManager::SetConfPath(str_chat_room_conf); //设置配置文件路径
    CacheManager *cache_manager = CacheManager::getInstance();
    if (!cache_manager) {
        LOG_ERROR <<"CacheManager init failed";
        return -1;
    }
 
    CDBManager::SetConfPath(str_chat_room_conf);   //设置配置文件路径
    CDBManager *db_manager = CDBManager::getInstance();
    if (!db_manager) {
        LOG_ERROR <<"DBManager init failed";
        return -1;
    }

    const char *http_bind_ip = "0.0.0.0";
    char *str_num_event_loops = config_file.GetConfigValue("num_event_loops");  
    int num_event_loops = atoi(str_num_event_loops);
    char *str_num_threads = config_file.GetConfigValue("num_threads");  
    int num_threads = atoi(str_num_threads);

    uint16_t http_bind_port = 8080;
    char *str_http_bind_port = config_file.GetConfigValue("http_bind_port");  
    http_bind_port = atoi(str_http_bind_port);
    

    char *str_timeout_ms = config_file.GetConfigValue("timeout_ms");  
    int timeout_ms = atoi(str_timeout_ms);
    std::cout << "timeout_ms: " << timeout_ms << std::endl;

    EventLoop loop;     //主循环
    InetAddress addr(http_bind_ip, http_bind_port);     // 注意别搞错位置了
    LOG_INFO << "port: " << http_bind_port;
    HttpServer server(&loop, addr, "HttpServer", num_event_loops, num_threads);
    server.start();

    loop.loop(timeout_ms); //1000ms

    return 0;
}    