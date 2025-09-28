#include "http_conn.h"
#include "api_reg.h"
#include "api_login.h"
#include "api_common.h"
#include <regex>
#include <fstream>
#include <sstream>

#include "muduo/base/Logging.h"

CHttpConn::CHttpConn(TcpConnectionPtr tcp_conn):
    tcp_conn_(tcp_conn)
{
    uuid_ = std::any_cast<uint32_t>(tcp_conn_->getContext());
    LOG_DEBUG << "构造CHttpConn uuid: "<< uuid_ ;
}

CHttpConn::~CHttpConn() {
    LOG_DEBUG << "析构CHttpConn uuid: "<< uuid_ ;
}


void CHttpConn::OnRead(Buffer *buf) // CHttpConn业务层面的OnRead
{
    const char *in_buf = buf->peek();
    int32_t len = buf->readableBytes();
    
    http_parser_.ParseHttpContent(in_buf, len);
    if(http_parser_.IsReadAll()) {
        string url = http_parser_.GetUrlString();
        string content = http_parser_.GetBodyContentString();
        LOG_INFO << "url: " << url << ", content: " << content;   

        if (strncmp(url.c_str(), "/api/login", 10) == 0) { // 登录
            _HandleLoginRequest(url, content);
        }   
        else if(strncmp(url.c_str(), "/api/create-account", 18) == 0) {   //  创建账号
            _HandleRegisterRequest(url, content);
        } 
        
        else {
            char *resp_content = new char[256];
            string str_json = "{\"code\": 1}"; 
            uint32_t len_json = str_json.size();
            //暂时先放这里
            #define HTTP_RESPONSE_REQ                                                     \
                "HTTP/1.1 404 OK\r\n"                                                      \
                "Connection:close\r\n"                                                     \
                "Content-Length:%d\r\n"                                                    \
                "Content-Type:application/json;charset=utf-8\r\n\r\n%s"
            snprintf(resp_content, 256, HTTP_RESPONSE_REQ, len_json, str_json.c_str()); 	
            tcp_conn_->send(resp_content);
        }
       
    }
 
}

void CHttpConn::send(const string &data) {
    LOG_DEBUG<< "send: " << data;
    tcp_conn_->send(data.c_str(), data.size());
}

/**
假设输入的 HTTP 请求头为：

GET /chat/subdir HTTP/1.1
Host: 127.0.0.1:8080
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
Sec-WebSocket-Version: 13

程序运行后会输出：
Subdirectory: /chat/subdir
 */
std::string  CHttpConn::getSubdirectoryFromHttpRequest(const std::string& httpRequest) {
    // 正则表达式匹配请求行中的路径部分
    std::regex pathRegex(R"(GET\s+([^\s]+)\s+HTTP\/1\.1)");
    std::smatch pathMatch;

    // 查找路径
    if (std::regex_search(httpRequest, pathMatch, pathRegex)) {
        return pathMatch[1];  // 返回路径部分
    }

    return "";  // 如果没有找到路径，返回空字符串
}

int CHttpConn::_HandleRegisterRequest(std::string& url, std::string& post_data)
{
    std::string resp_json;
    int ret = api_register_user(post_data, resp_json);
    char* http_data = new char[HTTP_RESPONSE_JSON_MAX];
    int http_code = 200;
    std::string http_code_msg;
    if (ret == 0) {
        LOG_INFO << "register success, cookie: " << resp_json;
        http_code = 204;
        http_code_msg = "No Content";
        snprintf(http_data, HTTP_RESPONSE_JSON_MAX, HTTP_RESPONSE_WITH_COOKIE, 
            http_code, http_code_msg.c_str(), resp_json.c_str(), 0, "");

    } else {
        http_code = 400;
        http_code_msg = "Bad Request";
        snprintf(http_data, HTTP_RESPONSE_JSON_MAX, HTTP_RESPONSE_WITH_CODE,
            http_code, http_code_msg.c_str(), resp_json.length(), resp_json.c_str());
        LOG_INFO << "register failed, ret: " << ret << ", resp_json: " << resp_json;
    }
    tcp_conn_->send(http_data);
    LOG_INFO << "send http data: " << http_data;
    delete[] http_data;
    return 0;
}

int CHttpConn::_HandleLoginRequest(std::string& url, std::string& post_data)
{
    std::string resp_json;
    int ret = api_login_user(post_data, resp_json);
    char* http_data = new char[HTTP_RESPONSE_JSON_MAX];
    int code = 200;
    std::string code_msg = "No Cotent";
    if (ret == 0) {
        snprintf(http_data, HTTP_RESPONSE_JSON_MAX, HTTP_RESPONSE_WITH_COOKIE,
            code, code_msg.c_str(), resp_json.c_str(), 0, "");
    } else {
        code = 400;
        code_msg = "Bad Request";
        snprintf(http_data, HTTP_RESPONSE_JSON_MAX, HTTP_RESPONSE_WITH_CODE,
            code, code_msg.c_str(), resp_json.length(), resp_json.c_str());
        LOG_INFO << "register failed, ret: " << ret << ", resp_json: " << resp_json;
    }
    tcp_conn_->send(http_data);
    LOG_INFO << "send http data: " << http_data;
    delete[] http_data;
    return 0;
}

int CHttpConn::_HandleHtml(string &url, string &post_data) {
    std::ifstream fileStream("index.html");
    if (!fileStream.is_open()) {
        std::cerr << "无法打开文件。" << std::endl;
    }
    std::stringstream buffer;
    buffer << fileStream.rdbuf();

    char *szContent = new char[HTTP_RESPONSE_JSON_MAX];
    uint32_t ulen = buffer.str().size();
    snprintf(szContent, HTTP_RESPONSE_JSON_MAX, HTTP_RESPONSE_HTML, ulen,
             buffer.str().c_str());

    tcp_conn_->send(szContent);
    delete[] szContent;
    return 0;
}

char htmlStr[] = "<!DOCTYPE html>\n"
                     "<html>\n"
                     "<head>\n"
                     "<title>Welcome to nginx!</title>\n"
                     "<style>\n"
                     "    body {\n"
                     "        width: 35em;\n"
                     "        margin: 0 auto;\n"
                     "        font-family: Tahoma, Verdana, Arial, sans-serif;\n"
                     "    }\n"
                     "</style>\n"
                     "</head>\n"
                     "<body>\n"
                     "<h1>Welcome to nginx!</h1>\n"
                     "<p>If you see this page,零声教育 the nginx web server is successfully installed and\n"
                     "working. Further configuration is required.</p>\n"
                     "<p>For online documentation and support please refer to\n"
                     "<a href=\"http://nginx.org/\">nginx.org</a>.<br/>\n"
                     "Commercial support is available at\n"
                     "<a href=\"http://nginx.com/\">nginx.com</a>.</p>\n"
                     "<p><em>Thank you for using nginx.</em></p>\n"
                     "</body>\n"
                     "</html>";

int CHttpConn::_HandleMemHtml(string &url, string &post_data) {
    
    char *szContent = new char[HTTP_RESPONSE_HTM_MAX];
  
    snprintf(szContent, HTTP_RESPONSE_HTM_MAX, HTTP_RESPONSE_HTML, strlen(htmlStr),
             htmlStr);

    tcp_conn_->send(szContent);
    delete[] szContent;
    return 0;
}
