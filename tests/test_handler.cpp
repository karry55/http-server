#include "handler.h"
#include "ai.h"
#include "check.h"          // ← 引入 CHECK / CHECK_EQ / test_summary
#include <string>

int main() {
    DB db(":memory:");      // 内存数据库：跑完自动消失，不留文件、不污染环境
    Cache cache;
    AI ai;
    Handler handler(db, cache, ai);

    // ---------- 1) GET / ----------
    {
        auto [status, body] = handler.handle("GET", "/", "");
        CHECK_EQ(status, std::string("200 OK"));
        CHECK_EQ(body, std::string(R"({"message": "home"})"));
    }

    // ---------- 2) GET /status ----------
    {
        auto [status, body] = handler.handle("GET", "/status", "");
        CHECK_EQ(status, std::string("200 OK"));
        CHECK_EQ(body, std::string(R"({"status": "ok"})"));
    }

    // ---------- 3) POST /todo 能创建 ----------
    {
        auto [status, body] = handler.handle("POST", "/todo", "学写测试");
        CHECK_EQ(status, std::string("200 OK"));
        CHECK_EQ(body, std::string(R"({"status": "created"})"));
    }

    // ---------- 4) GET /todos 返回的是数组 ----------
    {
        auto [status, body] = handler.handle("GET", "/todos", "");
        CHECK_EQ(status, std::string("200 OK"));
        CHECK(body.front() == '[');       // 以 [ 开头
        CHECK(body.back() == ']');        // 以 ] 结尾
        CHECK(body.find("学写测试") != std::string::npos);   // 刚才那条在里面
    }

    // ---------- 5) 不存在的路径 → 404 ----------
    {
        auto [status, body] = handler.handle("GET", "/no-such-path", "");
        CHECK_EQ(status, std::string("404 Not Found"));
    }

    // ---------- 6) 非法 id → 400（这一组就是崩溃的回归测试）----------
    {
        auto [status, body] = handler.handle("GET", "/todo/abc", "");
        CHECK_EQ(status, std::string("400 Bad Request"));
    }
    {
        // 超大数字：曾经会让整个进程崩溃
        auto [status, body] = handler.handle("GET", "/todo/99999999999999999999", "");
        CHECK_EQ(status, std::string("400 Bad Request"));
        CHECK_EQ(body, std::string(R"({"error": "invalid id"})"));
    }
    {
        auto [status, body] = handler.handle("GET", "/todo/0", "");
        CHECK_EQ(status, std::string("400 Bad Request"));
    }
    {
        // int 最大值：合法（虽然查不到），必须是 404 而不是 400
        auto [status, body] = handler.handle("GET", "/todo/2147483647", "");
        CHECK_EQ(status, std::string("404 Not Found"));
    }
    {
        // int 最大值 + 1：超范围，必须 400
        auto [status, body] = handler.handle("GET", "/todo/2147483648", "");
        CHECK_EQ(status, std::string("400 Bad Request"));
    }

    // ---------- 7) PUT / DELETE 的非法 id 也要挡住 ----------
    {
        auto [status, body] = handler.handle("PUT", "/todo/99999999999999999999", "x");
        CHECK_EQ(status, std::string("400 Bad Request"));
    }
    {
        auto [status, body] = handler.handle("DELETE", "/todo/99999999999999999999", "");
        CHECK_EQ(status, std::string("400 Bad Request"));
    }

    // ---------- 8) OPTIONS 预检 ----------
    {
        auto [status, body] = handler.handle("OPTIONS", "/todo/1", "");
        CHECK_EQ(status, std::string("204 No Content"));
    }

    return test_summary("test_handler");   // ← 关键：有失败就返回非 0
}