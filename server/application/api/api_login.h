#ifndef __CHATROOM_SERVER_APPLICATION_API_API_LOGIN_H__
#define __CHATROOM_SERVER_APPLICATION_API_API_LOGIN_H__

#include <string>
#include "api_common.h"

int decode_login_json(const std::string &str_json, std::string& email, std::string& password);
int encode_login_json(api_error_id input, std::string message, std::string& str_json);
int api_login_user(std::string& post_data, std::string& resp_json);

#endif