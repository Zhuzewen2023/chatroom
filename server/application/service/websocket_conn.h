#ifndef __WEBSOCKET_CONN_H__
#define __WEBSOCKET_CONN_H__

#include "http_conn.h"
#include <sstream> // 包含 istringstream 的头文件
#include <json/json.h>
#include <unordered_map>
#include <openssl/sha.h>
#include "muduo/base/Logging.h" // Logger日志头文件
#include "api_types.h"
#include "muduo/base/ThreadPool.h"

#define OPCODE_CONTINUATION_FRAME   0x0
#define OPCODE_TEXT_FRAME           0x1
#define OPCODE_BINARY_FRAME         0x2
#define OPCODE_CONNECTION_CLOSE     0x8
#define OPCODE_PING                 0x9
#define OPCODE_PONG                 0xA

class CWebSocketConn: public CHttpConn {
public:
    CWebSocketConn(const TcpConnectionPtr& conn);
    virtual ~CWebSocketConn();
    
    virtual void OnRead( Buffer* buf);
    void disconnect();
    static void InitThreadPool(int thread_num);  // 添加静态函数声明

private:
    void sendCloseFrame(uint16_t code, const std::string reason);
    void sendPongFrame(); // 发送 Pong 帧
    bool isCloseFrame(const std::string& frame); //是否是关闭帧

    int sendHelloMessage();
    int handleClientMessages(Json::Value &root);
    int handleRequestRoomHistory(Json::Value &root);
    int handleClientCreateRoom(Json::Value &root); 
    bool  handshake_completed_ = false; //握手是否完成
    int32_t userid_ = -1;   //
    std::string username_;           //用户名
    std::string email_;

    std::unordered_map<string, Room> rooms_map_;    //加入的房间
    std::string incomplete_frame_buffer_; // 用于存储不完整的WebSocket帧数据

    uint64_t stats_total_messages_ = 0;
    uint64_t stats_total_bytes_ = 0;

    static ThreadPool* s_thread_pool_;
};
using CWebSocketConnPtr = std::shared_ptr<CWebSocketConn>;
std::string buildWebSocketFrame(const std::string& payload, const uint8_t opcode = 0x01);
#endif