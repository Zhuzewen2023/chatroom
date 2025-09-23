#include "config_file_reader.h"
#include "muduo/base/Logging.h"

CConfigFileReader::CConfigFileReader(const std::string& file_name)
{
    _LoadFile(file_name);
}

CConfigFileReader::~CConfigFileReader() {}

char* CConfigFileReader::GetConfigValue(const char* name)
{
    if (!load_ok) {
        return NULL;
    }

    char *value = NULL;
    map<std::string, std::string>::iterator it = config_map_.find(name);
    if (it != config_map_.end()) {
        value = (char*)it->second.c_str();
    }

    return value;
}

int CConfigFileReader::SetConfigValue(const char* name ,const char* value)
{
    if (!load_ok) {
        return -1;
    }

    map<std::string, std::string>::iterator it = config_map_.find(name);
    if (it != config_map_.end()) {
        it->second = value;
    } else {
        config_map_.insert(std::make_pair(name, value));
    }
    return _WriteFile();
}

void CConfigFileReader::_LoadFile(const char *filename)
{
    config_file_.clear();
    config_file_.append(filename);
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        LOG_WARN<< "open file " << filename << " failed";
        return;
    }

    char buf[1024] = {0};
    while (1) {
        char *p = fgets(buf, sizeof(buf), fp);
        if (!p) {
            if (feof(fp)) {
                LOG_INFO << "read file " << filename << " finished";
            } else {
                LOG_WARN << "read file " << filename << " failed";
            }
            break;
        }
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') {
            buf[len-1] = '\0';
        }
        char *ch = strchr(buf, '#');
        if (ch) {
            *ch = '\0';
        }
        if (strlen(buf) == 0) {
            continue;
        }
        _ParseLine(buf);
    }
    fclose(fp);
    load_ok = true;
}

int CConfigFileReader::_WriteFile(const char *filename)
{
    FILE *fp = NULL;
    if (filename == NULL) {
        fp = fopen(config_file_.c_str(), "w");
    } else {
        fp = fopen(filename, "w");
    }
    if (!fp) {
        LOG_WARN << "open file " << filename << " failed";
        return -1;
    }
    char buf[1024] = {0};
    map<std::string, std::string>::iterator it = config_map_.begin();
    while (it != config_map_.end()) {
        memset(buf, 0, sizeof(buf));
        snprintf(buf, sizeof(buf), "%s=%s\n", it->first.c_str(), it->second.c_str());
        uint32_t ret = fwrite(buf, strlen(buf), 1, fp);
        if (ret != 1) {
            LOG_WARN << "write file " << filename << " failed";
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

void CConfigFileReader::_ParseLine(char *line)
{
    char* p = strchr(line, '=');
    if (p == NULL) {
        return;
    }
    *p = '\0';
    char *key = _TrimSpace(line);
    char *value = _TrimSpace(p+1);
    if (key && value) {
        config_map_.insert(std::make_pair(key, value));
    }
}

char* CConfigFileReader::_TrimSpace(char *str)
{
    char *start_pos = str;
    while (*start_pos == ' ' || *start_pos == '\t') {
        start_pos++;
    }
    if (strlen(start_pos) == 0) {
        return NULL;
    }
    char *end_pos = name + strlen(name) - 1;
    while (*end_pos == ' ' || *end_pos == '\t') {
        *end_pos = '\0';
        end_pos--;
    }

    int len = (int)(end_pos - start_pos) + 1;
    if (len <= 0) {
        return NULL;
    }
    return start_pos;
}

