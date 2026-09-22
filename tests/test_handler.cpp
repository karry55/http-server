#include "handler.h"
#include "ai.h"      // ← 新增
#include <iostream>

int main() {
    DB db("test_handler.db");
    Cache cache;
    AI ai;                              // ← 新增
    Handler handler(db, cache, ai);     // ← 3 个参数

    // GET /
    auto [status, body] = handler.handle("GET", "/", "");
    std::cout << status << ": " << body << std::endl;

    // POST /todo
    std::tie(status, body) = handler.handle("POST", "/todo", "buy milk");
    std::cout << status << ": " << body << std::endl;

    // GET /todos
    std::tie(status, body) = handler.handle("GET", "/todos", "");
    std::cout << status << ": " << body << std::endl;

    return 0;
}