#include "handler.h"
#include <iostream>

int main() {
    DB db("test_handler.db");
    Cache cache;
    Handler handler(db, cache);

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
