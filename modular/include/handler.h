#pragma once

#include <string>
#include "db.h"
#include "cache.h"

class Handler {
public:
    Handler(DB& db, Cache& cache);

    // 返回 (status_code, body)
    std::pair<std::string, std::string> handle(
        const std::string& method,
        const std::string& path,
        const std::string& body);

private:
    DB& db_;
    Cache& cache_;
};
