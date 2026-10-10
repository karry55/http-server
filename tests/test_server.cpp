#include "server.h"
#include "handler.h"
#include "db.h"
#include "cache.h"
#include "ai.h"
#include "check.h"
#include <iostream>

int main() {
    // 创建 Server 对象
    DB db("test_server.db");
    Cache cache;
    AI ai;
    Handler handler(db, cache, ai);
    Server server(8080, handler);

    // 测试 Server 构造成功
    CHECK(true);   // 如果构造没崩溃，说明成功

    std::cout << "Server 构造成功" << std::endl;

    return test_summary("test_server");
}