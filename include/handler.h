#pragma once
#include <string>
#include "db.h"
#include "cache.h"
#include "ai.h"

class Handler {
public:
    Handler(DB& db, Cache& cache, AI& ai);

    std::pair<std::string, std::string> handle(
        const std::string& method,
        const std::string& path,
        const std::string& body);

private:
    DB& db_;
    Cache& cache_;
    AI& ai_;
};