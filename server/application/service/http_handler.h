/**
 * 抽象一个http handler，在内部解析这个连接是普通的http，还是websocket
 */
#ifndef __HTTP_HANDLER_H__
#define __HTTP_HANDLER_H__
#include <muduo/net/TcpConnection.h>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cctype>
#include "websocket_conn.h"

using namespace muduo;
using namespace muduo::net;

class CHttpConn;
class CWebSocketConn;

class HttpHandler {
public:
    // 定义请求类型的枚举
    enum  RequestType {
        UNKNOWN,  // 未知类型
        HTTP,     // HTTP 请求
        WEBSOCKET // WebSocket 请求
    };
    HttpHandler(const TcpConnectionPtr& conn)
        : tcp_conn_(conn) 
    {
        LOG_INFO << "构造";
    }
    ~HttpHandler() {
        LOG_INFO << "析构";
        http_conn_.reset();
    }    

    void OnRead(Buffer* buf) {
        if(request_type_ == UNKNOWN) {
            const char *in_buf = buf->peek();
            int32_t len = buf->readableBytes();
            std::cout << "in_buf: " << in_buf << std::endl;

            auto headers = parseHttpHeaders(in_buf, len);
            if (isWebSocketRequest(headers)) {
                // WebSocket 请求
                request_type_ = WEBSOCKET;
                http_conn_ = std::make_shared<CWebSocketConn>(tcp_conn_);
                http_conn_->setHeaders(headers);
            } else {
                // HTTP 请求
                request_type_ = HTTP;
                http_conn_ = std::make_shared<CHttpConn>(tcp_conn_);
                http_conn_->setHeaders(headers);
            }
        }
        // 将数据交给具体的处理器
        if(http_conn_)
            http_conn_->OnRead(buf);
     }
    
private:

    std::unordered_map<std::string, std::string> parseHttpHeaders(const char *data, int size) {
        std::string request(data, size);
        return parseHttpHeaders(request);
    }
    // 解析 HTTP 请求头
    std::unordered_map<std::string, std::string> parseHttpHeaders(const std::string& request) {
        std::unordered_map<std::string, std::string> headers;
        std::istringstream stream(request);
        std::string line;

        while (std::getline(stream, line) && line != "\r") {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 2, line.size() - colon - 3); // 去掉 ": " 和 "\r"
                headers[key] = value;
            }
        }

        return headers;
    }

    // 判断是否是 WebSocket 请求
    bool isWebSocketRequest(const std::unordered_map<std::string, std::string>& headers) {
        auto upgradeIt = headers.find("Upgrade");
        auto connectionIt = headers.find("Connection");

        if (upgradeIt != headers.end() && connectionIt != headers.end()) {
            std::string upgrade = upgradeIt->second;
            std::string connection = connectionIt->second;

            // 将字符串转换为小写
            std::transform(upgrade.begin(), upgrade.end(), upgrade.begin(), ::tolower);
            std::transform(connection.begin(), connection.end(), connection.begin(), ::tolower);

            return upgrade == "websocket" && connection.find("upgrade") != std::string::npos;
        }

        return false;
    }
    TcpConnectionPtr tcp_conn_;
    CHttpConnPtr http_conn_; // 指向 CHttpConn 或 CWebSocketConn 的基类指针
    RequestType request_type_ = UNKNOWN;
};
using HttpHandlerPtr = std::shared_ptr<HttpHandler>;

#endif