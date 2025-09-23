#ifndef __CHATROOM_SERVER_APPLICATION_BASE_CONFIG_FILE_READER_H__
#define __CHATROOM_SERVER_APPLICATION_BASE_CONFIG_FILE_READER_H__

#include "util.h"
#include <string>
#include <map>

class CConfigFileReader
{
public:
    CConfigFileReader(const char* filename);
    ~CConfigFileReader();

    char *GetConfigValue(const char* name);
    int SetConfigValue(const char* name, const char* value);
    void _LoadFile(const char *filename);
    int _WriteFile(const char *filename);
    void _ParseLine(char *line);
    char* _TrimSpace(char *str);

private:
    bool load_ok_;
    map<std::string, std::string> config_map_;
    std::string config_file_;
}

#endif
