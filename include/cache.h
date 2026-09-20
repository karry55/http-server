#pragma once

#include <string>

class Cache {
public:
    Cache();
    ~Cache();

    std::string get(const std::string& key);
    void set(const std::string& key, const std::string& value, int ttl);
    void del(const std::string& key);

private:
    // 实际连接在 cpp 里用 thread_local
};
