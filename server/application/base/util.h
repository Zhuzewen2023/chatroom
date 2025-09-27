#ifndef __CHATROOM_SERVER_APPLICATION_BASE_UTIL_H__
#define __CHATROOM_SERVER_APPLICATION_BASE_UTIL_H__

#define _CRT_SECURE_NO_DEPRECATE // remove warning C4996,

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cstdint>
#ifndef _WIN32
#include <strings.h>
#endif

#include <assert.h>
#include <sys/stat.h>
#include <map>
#include <iostream>

using namespace std;
#ifdef _WIN32
#define snprintf sprintf_s
#else
#include <pthread.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#endif

#define NOTUSED_ARG(v)                                                         \
    ((void)v) // used this to remove warning C4100, unreferenced parameter

class CStrExplode
{
public:
    CStrExplode(char *str, char seperator);
    virtual ~CStrExplode();
    uint32_t GetItemCnt() {
        return item_cnt_;
    }
    char *GetItem(uint32_t idx) {
        return item_list_[idx];
    }
private:
    uint32_t item_cnt_;
    char** item_list_;
};

uint64_t GetTickCountMs();

void util_sleep(uint32_t millisecond);

char* ReplaceStr(char *src, char old_char, char new_char);

std::string Int2String(uint32_t num);

uint32_t String2Int(const std::string &value);

void ReplaceMark(std::string &str, const std::string& new_value, uint32_t &begin_pos);

void ReplaceMark(std::string& str, uint32_t new_value, uint32_t &begin_pos);

void WritePid();

inline unsigned char ToHex(const unsigned char& x);

inline unsigned char FromHex(const unsigned char& x);

/*这个 URLEncode 函数的作用是对输入字符串进行 URL 编码—— 
将 URL 中 “不安全” 的字符（如空格、&、=、中文等）
转换为 %XX 的十六进制格式（XX 是字符的 ASCII 码的十六进制表示），
确保字符串能在 URL 中安全传输。
*/
std::string URLEncode(const std::string& value);

std::string URLDecode(const std::string& value);

int64_t GetFileSize(const char* path);

const char* MemFind(const char* src_str, size_t src_len, 
                    const char* sub_str, size_t sub_len, bool flag);

#endif
