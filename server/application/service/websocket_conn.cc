#include "websocket_conn.h"
#include <cstring> //std::memcpy
#include "base64.h"
#include "api_common.h"
#include "api_msg.h"
#include "pub_sub_service.h"
// #include "api_room.h"
#include <thread>
#include <sstream> // 添加这个头文件
//key:userid,  value ：websocket的智能指针 基类
std::unordered_map<int32_t, CHttpConnPtr> s_user_ws_conn_map;
std::mutex s_mtx_user_ws_conn_map_;

//握手
std::string generateWebSocketHandshakeResponse(const std::string& key) {
    std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string accept_key = key + magic;

    unsigned char sha1[20]; //openssl的库
    SHA1(reinterpret_cast<const unsigned char*>(accept_key.data()), accept_key.size(), sha1);

    std::string accept = base64_encode(sha1, 20); //base 64编码

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";

    return response;
}

struct WebSocketFrame {
    bool fin;
    uint8_t opcode;
    bool mask;
    uint64_t payload_length;
    uint8_t masking_key[4];
    std::string payload_data;
};

// // 构造 WebSocket 数据帧
// std::string buildWebSocketFrame(const std::string& payload, const uint8_t opcode) {
//     std::string frame;
    
//     // 预分配足够的空间，避免多次内存分配
//     frame.reserve(payload.size() + 10);  // 10是头部可能的最大大小

//     // 第一个字节: FIN=1, RSV1-3=0, opcode
//     frame.push_back(0x80 | (opcode & 0x0F));

//     // 第二个字节: MASK=0(服务器发送的帧不需要掩码), payload len
//     size_t payload_length = payload.size();
//     if (payload_length <= 125) {
//         frame.push_back(static_cast<uint8_t>(payload_length));
//     } else if (payload_length <= 65535) {
//         frame.push_back(126);
//         frame.push_back(static_cast<uint8_t>((payload_length >> 8) & 0xFF));
//         frame.push_back(static_cast<uint8_t>(payload_length & 0xFF));
//     } else {
//         frame.push_back(127);
//         for (int i = 7; i >= 0; i--) {
//             frame.push_back(static_cast<uint8_t>((payload_length >> (8 * i)) & 0xFF));
//         }
//     }

//     // 添加payload数据，使用append而不是+=，避免可能的'\0'字符问题
//     frame.append(payload.data(), payload.size());

//     return frame;
// }

// 构造 WebSocket 数据帧
std::string buildWebSocketFrame(const std::string& payload, const uint8_t opcode) {
    std::string frame;
    //填充 FIN + RSV1-3 + Opcode 字段（第一个字节）
    frame.push_back(0x80 | (opcode & 0x0F));

    size_t payload_length = payload.size();
    /*MASK 位：服务端发送的帧不需要掩码（协议规定），
    所以第二个字节的最高位（MASK 位）始终为 0
    （上述代码中未显式设置，
    因为 payload_length 本身≤127 时，高位置 0；
    126/127 本身的二进制是 01111110/01111111，最高位也是 0）。*/
    if (payload_length <= 125) {
        /*情况 1：payload_length ≤ 125直接用第二个字节的低 7 位表示长度
        （最高位是 MASK 位，服务端发帧时 MASK=0）。
        例：长度为 10 → 第二个字节是 00001010（0x0A）。*/
        frame.push_back(static_cast<uint8_t>(payload_length));
    } else if (payload_length <= 65535) {
        /*情况 2：126 ≤ payload_length ≤ 655357 位最多表示 127，超过 125 时，
        先用第二个字节低 7 位设为 126（标记 “后续 2 字节是长度”），
        再用 2 个字节（16 位）表示实际长度（大端序，高位在前）。
        例：长度为 1000 → 16 位表示为 00000011 11101000 → 
        代码中先推 126，再推高 8 位 0x03，再推低 8 位 0xE8。*/
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((payload_length >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(payload_length & 0xFF));
    } else {
        /*超过 16 位能表示的最大值（65535）时，
        用第二个字节低 7 位设为 127（标记 “后续 8 字节是长度”），
        再用 8 个字节（64 位）表示实际长度（大端序）。*/
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back(static_cast<uint8_t>((payload_length >> (8 * i)) & 0xFF));
        }
    }

    frame += payload;

    return frame;
}

// /解析收到的数据并获取websocket  frame
WebSocketFrame parseWebSocketFrame(const std::string& data) {
    WebSocketFrame frame;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());

    // 解析第一个字节
    frame.fin = (bytes[0] & 0x80) != 0;
    frame.opcode = bytes[0] & 0x0F;

    // 解析第二个字节
    frame.mask = (bytes[1] & 0x80) != 0;
    frame.payload_length = bytes[1] & 0x7F;

    size_t offset = 2;

    // 解析扩展长度
    if (frame.payload_length == 126) {
        frame.payload_length = (bytes[2] << 8) | bytes[3];
        offset += 2;
    } else if (frame.payload_length == 127) {
        frame.payload_length = (static_cast<uint64_t>(bytes[2]) << 56) |
                               (static_cast<uint64_t>(bytes[3]) << 48) |
                               (static_cast<uint64_t>(bytes[4]) << 40) |
                               (static_cast<uint64_t>(bytes[5]) << 32) |
                               (static_cast<uint64_t>(bytes[6]) << 24) |
                               (static_cast<uint64_t>(bytes[7]) << 16) |
                               (static_cast<uint64_t>(bytes[8]) << 8) |
                               static_cast<uint64_t>(bytes[9]);
        offset += 8;
    }

    // 解析掩码
    if (frame.mask) {
        std::memcpy(frame.masking_key, bytes + offset, 4);
        offset += 4;
    }

    // 解析有效载荷数据
    frame.payload_data.assign(data.begin() + offset, data.end());

    // 如果掩码存在，解码数据
    if (frame.mask) {
        for (size_t i = 0; i < frame.payload_data.size(); i++) {
            frame.payload_data[i] ^= frame.masking_key[i % 4];
        }
    }

    return frame;
}


std::string extractSid(const std::string& input) {
    // 查找 "sid=" 的位置
    size_t sid_start = input.find("sid=");  // Cookie=sid=9c08f796-d33f-11ef-a42b-6363fea3e6a8
    if (sid_start == std::string::npos) {   // sid_start -> sid=9c08f796-d33f-11ef-a42b-6363fea3e6a8
        return ""; // 如果没有找到 "sid="，返回空字符串
    }

    // 跳过 "sid="，找到值的起始位置
    sid_start += 4; // "sid=" 的长度是 4 

    // 查找值的结束位置（假设以空格, &字符串, ;号 为结束）
    //find_first_of查找第一个出现的、属于 " &;" 这个字符集中的任意一个字符。
    size_t sid_end = input.find_first_of(" &;", sid_start); // Cookie=sid=9c08f796-d33f-11ef-a42b-6363fea3e6a8 & xxx
    if (sid_end == std::string::npos) {
        sid_end = input.length();   // Cookie=sid=9c08f796-d33f-11ef-a42b-6363fea3e6a8
    }

    // 提取 sid 的值
    std::string sid_value = input.substr(sid_start, sid_end - sid_start);
    return sid_value;
}


CWebSocketConn::CWebSocketConn(const TcpConnectionPtr& conn)
        : CHttpConn(conn) 
{
    LOG_INFO << "构造CWebSocketConn";
}

CWebSocketConn::~CWebSocketConn() {
    LOG_INFO << "析构CWebSocketConn " << ", userid_: " << userid_ << ", stats_total_messages_: " << stats_total_messages_ << ", stats_total_bytes_: " << stats_total_bytes_;
}

void CWebSocketConn::OnRead(Buffer* buf)
{
    if (!handshake_completed_) {
        //尚未握手
        std::string request = buf->retrieveAllAsString();
        //此处已经判断过Upgrade字段和Connection字段了
        LOG_INFO << "WebSocket Handshake";
        LOG_INFO << "request: " << request;
        size_t key_start = request.find("Sec-WebSocket-Key: ");
        if (key_start != std::string::npos) {
            //说明能找到
            key_start += strlen("Sec-WebSocket-Key: ");
            size_t key_end = request.find("\r\n", key_start);
            std::string ws_key = request.substr(key_start, key_end - key_start);
            LOG_INFO << "Get WebSocket Key: " << ws_key;
            std::string response = generateWebSocketHandshakeResponse(ws_key);
            send(response);
            handshake_completed_ = true;
            LOG_INFO << "web socket handshake_completed";

            std::string cookie = headers_["Cookie"];
            LOG_INFO << "Cookie: " << cookie;

            std::string sid;
            if (!cookie.empty()) {
                sid = extractSid(cookie);
            }
            LOG_INFO << "sid: " << sid;

            //做校验
            if (cookie.empty() || api_get_user_info_by_cookie(username_, userid_, email_, sid) < 0) {
                //校验失败
                std::string reason;
                if (email_.empty()) {
                    reason = "cookie validation failed, email is empty";
                } else {
                    reason = "cookie validation failed, username not found";
                }
                sendCloseFrame(1008, reason);
            } else {
                //校验成功
                //加入ws_user_conn_map
                {
                    std::lock_guard<std::mutex> lock(s_mtx_user_ws_conn_map_);
                    s_user_ws_conn_map.insert({userid_, shared_from_this()});
                }
                //订阅房间
                std::vector<Room> &room_list = PubSubService::GetRoomList();
                for (int i = 0; i < room_list.size(); i++) {
                    rooms_map_.insert({room_list[i].room_id, room_list[i]});
                }
                sendHelloMessage();
            }
        } else {
            LOG_ERROR << "no Sec-WebSocket-Key";
        }
        
    } else {
        //发送event_type = "hello"消息给客户端
        //handshake completed
        std::string request = buf->retrieveAllAsString();
        //解析websocket帧
        WebSocketFrame ws_frame = parseWebSocketFrame(request);
        LOG_INFO << "websocket handshake completed: " << ws_frame.payload_data;
        if (ws_frame.opcode == 0x01) {
            //文本帧
            LOG_DEBUG << "Process text frame, payload: " << ws_frame.payload_data;
            bool res;
            Json::Value root;
            Json::Reader jsonReader;
            res = jsonReader.parse(ws_frame.payload_data, root);
            if (!res) {
                LOG_WARN << "parse json failed ";
                return;
            } else {
                std::string type;
                if (root.isObject() && !root["type"].isNull()) {
                    type = root["type"].asString();
                    if (type == "clientMessages") {
                        handleClientMessages(root);
                    }
                }
            }
        } else if (ws_frame.opcode == 0x08) {
            LOG_INFO << "Receive close frame, closing connection...";
        }
    }
#if 0
    if (!handshake_completed_) {
        // 客户端 服务端 握手
        string request = buf->retrieveAllAsString();// 读取TCP缓冲区中所有数据（HTTP请求）
        //如果我们没有判断过是不是websocket的数据 是要做判断的，但目前已经判断过了
        LOG_DEBUG << "request" << request;
        size_t key_start = request.find("Sec-WebSocket-Key: ");
        if(key_start != std::string::npos) {
            //说明能找到Sec-WebSocket-Key: 
            key_start += 19;  //strlen("Sec-WebSocket-Key: ") -> 19 , key_start = Y90uUZxPYAVrEFJrtYEfCg==
                                        //                              Connection: Upgrade
            //结束位置\r\n
            size_t key_end = request.find("\r\n", key_start); // key_start -- key_end = Y90uUZxPYAVrEFJrtYEfCg==

            std::string key = request.substr(key_start, key_end - key_start);
            LOG_DEBUG << "key: " << key;
            string response = generateWebSocketHandshakeResponse(key);
            send(response); //握手成功
            handshake_completed_ = true;
            LOG_DEBUG << "handshake_completed_ ok";

            string Cookie = headers_["Cookie"];
            LOG_DEBUG << "Cookie: " << Cookie;
            string sid;
            if(!Cookie.empty()) {
                sid = extractSid(Cookie); 
            }
            LOG_DEBUG << "sid: " << sid;
            /*  {
                "email": "200@qq.com",
                "password": "b59c67bf196a4758191e42f76670ceba"
                }
                密码 1111
                sid=f88e0b78-d583-11ef-a42b-6363fea3e6a8
                */
            // 做校验 
            // 获取 用户名 用户id Email ，根据cookie-sid
            string email;
            if(Cookie.empty() || ApiGetUserInfoByCookie(username_, userid_, email, sid) < 0) {
                string reason;
                if(email.empty()) {
                    reason = "cookie validation failed";
                } else {
                    reason = "db  may be has issue";
                }
                // 校验失败
                LOG_WARN << reason;
                sendCloseFrame(1008, reason);
            } else {
                //校验成功
                // 把连接加入 s_user_ws_conn_map
                LOG_DEBUG << "cookie validation ok";
                s_mtx_user_ws_conn_map_.lock();
                s_user_ws_conn_map.insert({userid_,shared_from_this()});  // 同样userid连接可能已经存在了
                s_mtx_user_ws_conn_map_.unlock();
                //订阅房间
                std::vector<Room> &room_list = PubSubService::GetRoomList(); 
                for(int i = 0; i < room_list.size(); i++) {
                    rooms_map_.insert({room_list[i].room_id, room_list[i]});
                    PubSubService::GetInstance().AddSubscriber(room_list[i].room_id, userid_);// 订阅对应的聊天室
                }
                 // 发送信息给 客户端  
                sendHelloMessage();
            }
        } else {
            LOG_ERROR << "no Sec-WebSocket-Key";    
        }
        //发送hello type消息给到客户端
    } else {
        incomplete_frame_buffer_ += buf->retrieveAllAsString();
        LOG_DEBUG << "Current buffer length: " << incomplete_frame_buffer_.length();
        
        while (!incomplete_frame_buffer_.empty()) {
            // 检查数据是否足够解析基本头部(至少2字节)
            if (incomplete_frame_buffer_.length() < 2) {
                LOG_DEBUG << "Not enough data for frame header, waiting for more...";
                return;
            }
            
            // 计算需要的总长度
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(incomplete_frame_buffer_.data());
            uint64_t payload_len = bytes[1] & 0x7F;
            size_t header_length = 2;
            
            // 计算扩展长度字段的大小
            if (payload_len == 126) {
                if (incomplete_frame_buffer_.length() < 4) {
                    LOG_DEBUG << "Not enough data for extended length(2 bytes), waiting for more...";
                    return;
                }
                header_length += 2;
                payload_len = (bytes[2] << 8) | bytes[3];
            } else if (payload_len == 127) {
                if (incomplete_frame_buffer_.length() < 10) {
                    LOG_DEBUG << "Not enough data for extended length(8 bytes), waiting for more...";
                    return;
                }
                header_length += 8;
                payload_len = ((uint64_t)bytes[2] << 56) | ((uint64_t)bytes[3] << 48) |
                            ((uint64_t)bytes[4] << 40) | ((uint64_t)bytes[5] << 32) |
                            ((uint64_t)bytes[6] << 24) | ((uint64_t)bytes[7] << 16) |
                            ((uint64_t)bytes[8] << 8)  | bytes[9];
            }
            
            // 检查是否有掩码
            bool has_mask = (bytes[1] & 0x80) != 0;
            if (has_mask) {
                header_length += 4;
            }
            
            // 计算整个帧需要的总长度
            size_t total_frame_length = header_length + payload_len;
            
            // 检查是否有足够的数据构成完整帧
            if (incomplete_frame_buffer_.length() < total_frame_length) {
                LOG_DEBUG << "Not enough data for complete frame, waiting for more... "
                        << "Need: " << total_frame_length 
                        << ", Have: " << incomplete_frame_buffer_.length();
                return;
            }
            
            // 当有完整的帧时,将处理逻辑提交给线程池
            if (incomplete_frame_buffer_.length() >= total_frame_length) {
                // 复制当前帧数据
                string frame_data = incomplete_frame_buffer_.substr(0, total_frame_length);
                
                // 移除已处理的帧数据
                incomplete_frame_buffer_ = incomplete_frame_buffer_.substr(total_frame_length);
                
                // 获取shared_from_this()的副本
                auto self = shared_from_this();
                
                // 提交任务到线程池
                s_thread_pool_->run([this, self, frame_data]() {
                    // LOG_INFO << "run in thread pool";
                    // 解析并处理完整帧
                    WebSocketFrame frame = parseWebSocketFrame(frame_data);
                    stats_total_messages_++;
                    stats_total_bytes_ += frame.payload_data.size();
                    
                    // 打印当前线程id
                    std::ostringstream oss;
                    oss << std::this_thread::get_id(); // 将线程ID转换为字符串
                    LOG_DEBUG << "pool th_id: " << oss.str() << ", stats_total_messages_: " << stats_total_messages_ << ", stats_total_bytes_: " << stats_total_bytes_;
                    
                    // 处理当前帧
                    if (frame.opcode == 0x01) { // 文本帧
                        LOG_DEBUG << "Process text frame, payload: " << frame.payload_data;
                        
                        bool res;
                        Json::Value root;
                        Json::Reader jsonReader;
                        res = jsonReader.parse(frame.payload_data, root);
                        if (!res) {
                            LOG_WARN << "parse json failed ";
                            return;
                        } else {
                            string type;
                            if (root.isObject() &&  !root["type"].isNull()) {
                                type = root["type"].asString();
                                if(type == "clientMessages") {
                                    handleClientMessages(root);
                                } else if(type == "requestRoomHistory") {
                                    handleRequestRoomHistory(root);
                                } else if(type == "clientCreateRoom") {
                                    handleClientCreateRoom(root);
                                }
                            } else {
                                LOG_ERROR << "data no a json object";
                            }
                        }
                    } else if (frame.opcode == 0x08) { // 关闭帧
                        LOG_DEBUG << "Received close frame, closing connection...";
                        disconnect();
                    }
                });
            }
        }
    }
#endif
}

// 发送 WebSocket 关闭帧
void CWebSocketConn::sendCloseFrame(uint16_t code, const std::string reason) 
{
    if (!tcp_conn_) return;

    // 构造关闭帧
    char frame[2 + reason.size()];
    frame[0] = (code >> 8) & 0xFF;  // 状态码高位
    frame[1] = code & 0xFF;         // 状态码低位
    std::memcpy(frame + 2, reason.data(), reason.size());

    // 发送关闭帧
    tcp_conn_->send(frame, sizeof(frame));
}

int CWebSocketConn::sendHelloMessage()
{
    /*
    {
        "type" : "hello",
        "payload" : {
            "me": {
                "id": 
                "username":
            },
            "rooms": {
                "id": 
                "name":
                "hasMoreMessages":
                "messages": [
                    {
                        "id":
                        "content"
                        "user": {
                            "id":
                            "username":
                        },
                        "timestamp":
                    },
                    {

                    }
                    ...
                ]
            }
        }
    }
    */
    Json::Value root;
    root["type"] = "hello";
    
    Json::Value payload;
    Json::Value me;
    me["id"] = userid_;
    me["username"] = username_;
    payload["me"] = me;
    Json::Value rooms;
    int it_index = 0;
    for (auto it = rooms_map_.begin(); it != rooms_map_.end(); ++it) {
        Room& room_item = it->second;
        std::string last_message_id;
        MessageBatch message_batch;
        api_get_room_history(room_item, message_batch);
        LOG_INFO << "room: " << room_item.room_name << ", history_last_message_id: " << it->second.history_last_message_id;
        Json::Value room;
        room["id"] = room_item.room_id;
        room["name"] = room_item.room_name;
        room["hasMoreMessages"] = message_batch.has_more;
        Json::Value messages;
        for (int j = 0; j < message_batch.messages.size(); j++) {
            Json::Value message;
            Json::Value user;
            message["id"] = message_batch.messages[j].id;
            message["content"] = message_batch.messages[j].content;
            user["id"] = userid_;
            user["username"] = username_;
            message["user"] = user;
            message["timestamp"] = (Json::UInt64)message_batch.messages[j].timestamp;
            messages[j] = message;
        }
        if (message_batch.messages.size() > 0) {
            room["messages"] = messages;
        } else {
            /*如果不这样处理（比如直接不赋值或赋值为默认值），
            当没有消息时，room["messages"] 可能会被默认解析为 null
            （JsonCpp 中未赋值的 Json::Value 默认是 null）。
            这会导致 JSON 结构中该字段的类型在 “数组” 和 “null” 之间波动。*/
            room["messages"] = Json::arrayValue; /*Json::arrayValue 是一个预定义的静态常量，用于表示一个空的 JSON 数组*/
        }
        rooms[it_index] = room;
        it_index++;
    }

    root["payload"] = payload;

    Json::FastWriter writer;
    string str_json = writer.write(root);
    // 打印 JSON 字符串
    LOG_INFO << "Serialized JSON: " << str_json;
    string hello = buildWebSocketFrame(str_json, OPCODE_TEXT_FRAME);
    send(hello);    //能否直接发送给客户端？
    return 0;
#if 0
    // 固定的房间信息在哪里？
    Json::Value root;
    root["type"] = "hello";
    Json::Value payload;
    Json::Value me;
    me["id"] = userid_;
    me["username"] = username_;
    payload["me"] = me;

    Json::Value rooms;
    int it_index = 0;
    for (auto it = rooms_map_.begin(); it != rooms_map_.end(); ++it)
    {
        Room &room_item = it->second;
        string  last_message_id;  
        MessageBatch  message_batch;
         // 获取房间的消息
        ApiGetRoomHistory(room_item, message_batch);   
        LOG_DEBUG << "room: " << room_item.room_name << ", history_last_message_id:" << it->second.history_last_message_id;
        Json::Value  room;  
        room["id"] = room_item.room_id;      //聊天室主题名称
        room["name"] = room_item.room_name;      //聊天室主题名称 先设置成一样的
        room["hasMoreMessages"] = message_batch.has_more;
        Json::Value  messages; 
        for(int j = 0; j < message_batch.messages.size(); j++) {
            Json::Value  message;
            Json::Value user;
            message["id"] = message_batch.messages[j].id;
            message["content"] = message_batch.messages[j].content;   
            user["id"] = (Json::Int64)message_batch.messages[j].user_id;   //这里该获取对应消息的id    
            user["username"] = message_batch.messages[j].username;  
            message["user"] = user;
            message["timestamp"] = (Json::UInt64)message_batch.messages[j].timestamp;
            messages[j] = message;
        }
        if(message_batch.messages.size() > 0)
            room["messages"] = messages;
        else 
            room["messages"] = Json::arrayValue;  //不能为NULL，否则前端报异常
        rooms[it_index] = room;
        it_index++;
    }
    payload["rooms"] = rooms;
    root["payload"] = payload;
    Json::FastWriter writer;
    string str_json = writer.write(root);

    
    // 打印 JSON 字符串
    LOG_DEBUG << "Serialized JSON: " << str_json;
    string hello = buildWebSocketFrame(str_json);
    send(hello);    //能否直接发送给客户端？
    return 0;
#endif
}

// 添加获取当前时间戳的辅助函数
uint64_t getCurrentTimestamp() {
    auto now = std::chrono::system_clock::time_point::clock::now();
    auto duration = now.time_since_epoch();
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    return milliseconds.count(); //单位是毫秒
}
 



/**
 * {
  "type": "clientMessages",
  "payload": {
    "roomId": "beast",
    "messages": [
      {
        "content": "这是小鸭子发送的消息"
      }
    ]
  }
}
 */
int CWebSocketConn::handleClientMessages(Json::Value &root)
{
    /*
        "type": "clientMessages",
        "payload": {
            "roomId": "",
            "messages": [
                {
                    "content": ""
                }
            ]
        }
    */
    std::string room_id;
    if(root["payload"].isNull()) {
        LOG_ERROR << "handleClientMessages failed, payload is NULL";
        return -1;
    }
    Json::Value payload = root["payload"];
    if(payload["roomId"].isNull()) {
        LOG_ERROR << "handleClientMessages failed, roomId is NULL";
        return -1;
    }
    room_id = payload["roomId"].asString();
    if(payload["messages"].isNull()) {
        LOG_ERROR << "handleClientMessages failed, messages is NULL";
        return -1;
    }
    Json::Value arrayObj = payload["messages"];
    std::vector<Message> msgs;  
    uint64_t timestamp = getCurrentTimestamp();
    for (int i = 0; i < arrayObj.size(); i++) {
        Json::Value message = arrayObj[i];
        Message msg;
        msg.content = message["content"].asString();
        msg.timestamp = timestamp;
        msg.user_id = userid_;
        msg.username = username_;
        msgs.push_back(msg);
    }

    //写入redis
    api_store_message(room_id, msgs);

#if 0
    // 把消息解析出来
    string room_id;
    if(root["payload"].isNull()) {
        return -1;
    }
    Json::Value payload = root["payload"];
    if(payload["roomId"].isNull()) {
        return -1;
    }
    room_id = payload["roomId"].asString();
    if(payload["messages"].isNull()) {
        return -1;
    }
    Json::Value arrayObj = payload["messages"];
    if(arrayObj.isNull()) {
        return -1;
    }
    std::vector<Message> msgs;  
    uint64_t timestamp = getCurrentTimestamp();
    for(int i = 0; i < arrayObj.size(); i++) {
        Json::Value message = arrayObj[i];
        Message msg;
        msg.content = message["content"].asString();
        msg.timestamp = timestamp;
        msg.user_id = userid_;
        msg.username = username_;
        msgs.push_back(msg);
    }
    //写到redis里面
    ApiStoreMessage(room_id, msgs);
    root = Json::Value();
    payload = Json::Value();
    root["type"] = "serverMessages";
    payload["roomId"]  = room_id;
    Json::Value messages;
    //for(Message msg: msgs)
    for(int j = 0; j < msgs.size(); j++) {
        Json::Value  message;
        Json::Value user;
        message["id"] = msgs[j].id;
        message["content"] = msgs[j].content;   
        user["id"] = userid_;
        user["username"] = username_;
        message["user"] = user;
        message["timestamp"] = (Json::UInt64)msgs[j].timestamp;
        messages[j] = message;
    }
    if(msgs.size() > 0)
        payload["messages"] = messages;
    else
        payload["messages"] = Json::arrayValue;
    root["payload"] = payload;
    Json::FastWriter writer;
    string str_json = writer.write(root);
    LOG_DEBUG << "serverMessages: " << str_json;
    std::string response = buildWebSocketFrame(str_json);
     auto callback = [&response, &room_id, this](const std::unordered_set<uint32_t> user_ids) {
        LOG_DEBUG << "room_id:" << room_id << ", callback " <<  ", user_ids.size(): " << user_ids.size();
        for (uint32_t userId: user_ids)
        {
             CHttpConnPtr ws_conn_ptr = nullptr;
             {
                std::lock_guard<std::mutex> ulock(s_mtx_user_ws_conn_map_); //自动释放
                ws_conn_ptr =  s_user_ws_conn_map[userId];
             }
             if(ws_conn_ptr) {
                ws_conn_ptr->send(response); //暂时先只收取数据
             } else
             {
                LOG_WARN << "can't find userid: " << userId;
             }
            /* code */
        }
        
     };

    // 广播给所有人
    PubSubService::GetInstance().PublishMessage(room_id, callback);
    return 0;
#endif
}

/**
 * 
 * {
    "type": "requestRoomHistory",
    "payload": {
        "roomId": "3bb1b0b6-e91c-11ef-ba07-bd8c0260908d",
        "firstMessageId": "1739364544747-0",
        "count": 30
    }
}
 */
int CWebSocketConn::handleRequestRoomHistory(Json::Value &root)
{
#if 0
    // [key] = value  -》 rapidjson
    string roomId = root["payload"]["roomId"].asString();
    string firstMessageId = root["payload"]["firstMessageId"].asString();
    int count  = root["payload"]["count"].asInt();

    //从redis读取历史消息
    Room &room_ = rooms_map_[roomId];
    MessageBatch  message_batch; 
    room_.history_last_message_id = firstMessageId;
    LOG_DEBUG << "1  room_id:" << roomId << ", history_last_message_id:" << room_.history_last_message_id;
    
    int ret = ApiGetRoomHistory(room_, message_batch, count);
    if(ret < 0) {
        LOG_ERROR << "ApiGetRoomHistory failed";
        return -1;
    }
    LOG_DEBUG << "2  room_id:" << roomId << ", history_last_message_id:" << room_.history_last_message_id;

    //封装消息
    root = Json::Value(); //重新置空
    Json::Value payload;

    root["type"] = "serverRoomHistory";
    payload["roomId"] = roomId;   //修复bug
    payload["name"] = room_.room_name;
    payload["hasMoreMessages"] = message_batch.has_more;

    Json::Value  messages; 
    for(int j = 0; j < message_batch.messages.size(); j++) {
        Json::Value  message;
        Json::Value user;
        message["id"] = message_batch.messages[j].id;
        message["content"] = message_batch.messages[j].content;   
        user["id"] = (Json::Int64)message_batch.messages[j].user_id;   //这里该获取对应消息的id    
        user["username"] = message_batch.messages[j].username;  
        message["user"] = user;
        message["timestamp"] = (Json::UInt64)message_batch.messages[j].timestamp;
        messages[j] = message;
    }
    if(message_batch.messages.size() > 0)
        payload["messages"] = messages;
    else 
        payload["messages"] = Json::arrayValue;  //不能为NULL，否则前端报异常

    root["payload"] = payload;
    //json序列化
     Json::FastWriter writer;
    string json_str = writer.write(root);
    string response = buildWebSocketFrame(json_str);
    
     //拉取消息只需要发给自己
    send(response);

    return 0;
#endif
}
//请求格式： {"type":"clientCreateRoom","payload":{"roomName":"dpdk教程"}} 
//响应格式： {"type":"serverCreateRoom","payload":{"roomId":"3bb1b0b6-e91c-11ef-ba07-bd8c0260908d", "roomName":"dpdk教程"}}
int CWebSocketConn::handleClientCreateRoom(Json::Value &root)
{
#if 0
     LOG_INFO << "handleClientCreateRoom into";
    // 把消息解析出来
    string roomId;
    string roomName;
    Json::Value payload = root["payload"];

    if(payload.isNull()) {
        LOG_WARN << "payload is null";
        return -1;
    }
    // 解析json  解析聊天室的名字 
    if(payload["roomName"].isNull()) {
        LOG_WARN << "roomName is null";
        return -1;
    }
    roomName = payload["roomName"].asString();
    // 分配房间id
    roomId = generateUUID();
    LOG_INFO << "handleClientCreateRoom, roomName: " << roomName << ", roomId: " << roomId;
    

    //存储到数据库
    std::string error_msg;
    bool ret = ApiCreateRoom(roomId, roomName, userid_, error_msg);
    if(!ret ) {
        LOG_ERROR << "ApiCreateRoom failed: " << error_msg;
        return -1;
    }

    PubSubService::GetInstance().AddRoomTopic(roomId, roomName, userid_);
    // 把新建的聊天室加入到 room_list
    Room room;
    room.room_id = roomId;
    room.room_name = roomName;
    room.create_time = getCurrentTimestamp();
    room.creator_id = userid_;
    PubSubService::AddRoom(room);

    //每个人都订阅这个聊天室
    {
        std::lock_guard<std::mutex> lock(s_mtx_user_ws_conn_map_);
        rooms_map_.insert({roomId, room});
        for(auto it = s_user_ws_conn_map.begin(); it != s_user_ws_conn_map.end(); ++it) {
            //房间id， 用户id 订阅
            LOG_DEBUG << "AddSubscriber: " << roomId << ", userid: " << it->first;
            PubSubService::GetInstance().AddSubscriber(roomId, it->first);
        }
    }

    //广播给所有的人
    //先序列化消息
    // Json::Value root;
    root = Json::Value(); //重新置空
    // Json::Value payload;
    payload = Json::Value(); //重新置空
    root["type"] = "serverCreateRoom";
    payload["roomId"] = roomId;
    payload["roomName"] = roomName;
    root["payload"] = payload;
     //json序列化
    Json::FastWriter writer;
    string json_str = writer.write(root);
    LOG_INFO << "serverCreateRoom: " << json_str;
    string response = buildWebSocketFrame(json_str);

    // 发送给所有的人
    auto callback = [&response, &roomId, this](const std::unordered_set<uint32_t> &user_ids) {
        LOG_INFO << "room_id:" << roomId << ", callback " <<  ", user_ids.size(): " << user_ids.size();
        for (uint32_t userId: user_ids) {
            CHttpConnPtr  ws_conn_ptr = nullptr;
            {
                std::lock_guard<std::mutex> ulock(s_mtx_user_ws_conn_map_); //自动释放
                ws_conn_ptr = s_user_ws_conn_map[userId];
            }
            if(ws_conn_ptr) {
                ws_conn_ptr->send(response);
            } else {
                LOG_WARN << "can't find userid: " << userId;
            }
        }
    };


    PubSubService::GetInstance().PublishMessage(roomId, callback);

    return 0;
#endif
}

void CWebSocketConn::disconnect()
{
    if(tcp_conn_) {
        LOG_ERROR << "tcp_conn_ sendCloseFrame";
        sendCloseFrame(1000, "Normal close");
        tcp_conn_->shutdown();
    }
    {
        // 将当前的user-CWebSocketConn从 s_mtx_user_ws_conn_map_移除
        std::lock_guard lck(s_mtx_user_ws_conn_map_);
        auto it = s_user_ws_conn_map.find(userid_);
        if(it != s_user_ws_conn_map.end()) {
            LOG_DEBUG << "disconnect, userid: " << userid_ << " from s_user_ws_conn_map erase";
            LOG_DEBUG << "1 s_user_ws_conn_map.size(): " << s_user_ws_conn_map.size();
            s_user_ws_conn_map.erase(it);
            LOG_DEBUG << "2 s_user_ws_conn_map.size(): " << s_user_ws_conn_map.size();
        }
    }
}

ThreadPool* CWebSocketConn::s_thread_pool_ = nullptr;

void CWebSocketConn::InitThreadPool(int thread_num) {
    if(!s_thread_pool_) {
        s_thread_pool_ = new ThreadPool("WebSocketConnThreadPool");
        s_thread_pool_->start(thread_num);
    }
}