#ifndef __CHATROOM_API_API_REG_H__
#define __CHATROOM_API_API_REG_H__

#include <string>

enum class api_error_id
{
    bad_request = 0,
    login_failed,
    email_exists,
    username_exists,  
};

int api_register_user(std::string &post_data, std::string &response_data);

#endif
