#ifndef __CHATROOM_SERVER_APPLICATION_SERVICE_HTTP_CONN_H__
#define __CHATROOM_SERVER_APPLICATION_SERVICE_HTTP_CONN_H__

#include "http_parser_wrapper.h"
#include "muduo/net/TcpConnection.h"
#include "muduo/net/Buffer.h"

#define HTTP_RESPONSE_JSON_MAX 4096
#define HTTP_RESPONSE_JSON                                                     \
    "HTTP/1.1 200 OK\r\n"                                                      \
    "Connection:close\r\n"                                                     \
    "Content-Length:%d\r\n"                                                    \
    "Content-Type:application/json;charset=utf-8\r\n\r\n%s"

#define HTTP_RESPONSE_WITH_CODE                                                     \
    "HTTP/1.1 %d %s\r\n"                                                      \
    "Connection:close\r\n"                                                     \
    "Content-Length:%d\r\n"                                                    \
    "Content-Type:application/json;charset=utf-8\r\n\r\n%s"

// 86400单位是秒，86400换算后是24小时
#define HTTP_RESPONSE_WITH_COOKIE                                                    \
    "HTTP/1.1 %d %s\r\n"                                                      \
    "Connection:close\r\n"                                                     \
    "set-cookie: sid=%s; HttpOnly; Max-Age=86400; SameSite=Strict\r\n" \
    "Content-Length:%d\r\n"                                                    \
    "Content-Type:application/json;charset=utf-8\r\n\r\n%s"


#define HTTP_RESPONSE_HTM_MAX 4096
#define HTTP_RESPONSE_HTML                                                    \
    "HTTP/1.1 200 OK\r\n"                                                      \
    "Connection:close\r\n"                                                     \
    "Content-Length:%d\r\n"                                                    \
    "Content-Type:text/html;charset=utf-8\r\n\r\n%s"


#define HTTP_RESPONSE_BAD_REQ                                                     \
    "HTTP/1.1 400 Bad\r\n"                                                      \
    "Connection:close\r\n"                                                     \
    "Content-Length:%d\r\n"                                                    \
    "Content-Type:application/json;charset=utf-8\r\n\r\n%s"

    
using namespace muduo;
using namespace muduo::net;
using namespace std;
class CHttpConn :  public std::enable_shared_from_this<CHttpConn>
{
public:
    CHttpConn(TcpConnectionPtr tcp_conn);
    virtual ~CHttpConn(); 
    virtual void OnRead(Buffer *buf);
    virtual std::string getSubdirectoryFromHttpRequest(const std::string& httpRequest);
    virtual void setHeaders(std::unordered_map<std::string, std::string> &headers) {
        headers_ = headers;
    }
    void send(const string &data);
protected:
    TcpConnectionPtr tcp_conn_;
    uint32_t uuid_ = 0;
    CHttpParserWrapper http_parser_;
    string url_;
    std::unordered_map<std::string, std::string> headers_;
private:
 // 账号注册处理
    int _HandleRegisterRequest(string &url, string &post_data);
    // 账号登陆处理
    int _HandleLoginRequest(string &url, string &post_data);
 
    int _HandleHtml(string &url, string &post_data);
    int _HandleMemHtml(string &url, string &post_data);
 
};

using CHttpConnPtr = std::shared_ptr<CHttpConn>;

#endif