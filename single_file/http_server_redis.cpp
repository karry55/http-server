#include <iostream>
#include <string>
#include <cstring>
#include <cctype>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <hiredis/hiredis.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_EVENTS 1024

// ==================== 线程池 ====================
class ThreadPool {
public:
    ThreadPool(size_t n) : stop_(false) {
        for (size_t i = 0; i < n; i++) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mtx_);
                        cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) w.join();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_;
};

// ==================== 全局 ====================
sqlite3* g_db;

// ===== 修改 1：每个线程独立 Redis 连接 =====
thread_local redisContext* t_redis = nullptr;

redisContext* get_redis() {
    if (t_redis == nullptr || t_redis->err) {
        t_redis = redisConnect("127.0.0.1", 6379);
    }
    return t_redis;
}

// ==================== 处理请求 ====================
void handle_request(int fd, std::string request) {
    // 解析请求行
    std::string method, path;
    size_t pos1 = request.find(' ');
    size_t pos2 = request.find(' ', pos1 + 1);
    if (pos1 != std::string::npos && pos2 != std::string::npos) {
        method = request.substr(0, pos1);
        path   = request.substr(pos1 + 1, pos2 - pos1 - 1);
    }

    size_t q = path.find('?');
    if (q != std::string::npos) path = path.substr(0, q);

    size_t body_pos = request.find("\r\n\r\n");
    std::string req_body;
    if (body_pos != std::string::npos) {
        req_body = request.substr(body_pos + 4);
    }

    std::cout << "方法: " << method << ", 路径: " << path << std::endl;

    std::string body;
    std::string status = "200 OK";

    if (method == "GET" && path == "/") {
        body = R"({"message": "home"})";
    } else if (method == "GET" && path == "/status") {
        body = R"({"status": "ok"})";
    } else if (method == "POST" && path == "/todo") {
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(g_db, "INSERT INTO todos (content) VALUES (?);", -1, &stmt, nullptr) != SQLITE_OK) {
            body = R"({"error": "prepare failed"})";
            status = "500 Internal Server Error";
        } else {
            sqlite3_bind_text(stmt, 1, req_body.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                body = R"({"error": "insert failed"})";
                status = "500 Internal Server Error";
            } else {
                body = R"({"status": "created"})";
            }
            sqlite3_finalize(stmt);
        }
        // ===== 修改 2：用 get_redis()，删缓存 =====
        redisCommand(get_redis(), "DEL todos_cache");
    } else if (method == "GET" && path == "/todos") {
        // ===== 修改 3：用 get_redis()，先查缓存 =====
        redisReply* reply = (redisReply*)redisCommand(get_redis(), "GET todos_cache");
        if (reply && reply->type == REDIS_REPLY_STRING) {
            body = reply->str;
            freeReplyObject(reply);
        } else {
            if (reply) freeReplyObject(reply);

            // 缓存没有，查 SQLite
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(g_db, "SELECT id, content FROM todos;", -1, &stmt, nullptr) != SQLITE_OK) {
                body = R"({"error": "query failed"})";
                status = "500 Internal Server Error";
            } else {
                body = "[";
                bool first = true;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    int id = sqlite3_column_int(stmt, 0);
                    const char* content = (const char*)sqlite3_column_text(stmt, 1);
                    if (!first) body += ",";
                    body += R"({"id": )" + std::to_string(id) + R"(, "content": ")" + (content ? content : "") + R"("})";
                    first = false;
                }
                body += "]";
                sqlite3_finalize(stmt);
            }

            // ===== 修改 4：用 %b（二进制安全）代替 %s =====
            redisCommand(get_redis(), "SETEX todos_cache 60 %b", body.c_str(), body.size());
        }
    } else {
        body = R"({"error": "not found"})";
        status = "404 Not Found";
    }

    // 响应
    std::string response =
        "HTTP/1.1 " + status + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    write(fd, response.c_str(), response.size());
    close(fd);
}

// ==================== main ====================
int main() {
    // 1. 创建 socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(server_fd, 10) < 0) { perror("listen"); return 1; }

    // 2. 初始化数据库
    if (sqlite3_open("todo.db", &g_db) != SQLITE_OK) {
        std::cerr << "无法打开数据库" << std::endl; return 1;
    }
    sqlite3_exec(g_db,
        "CREATE TABLE IF NOT EXISTS todos ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "content TEXT NOT NULL"
        ");", nullptr, nullptr, nullptr);

    // ===== 修改 5：不再全局连接 Redis，改成每线程独立 =====
    // 删除原来的 g_redis = redisConnect(...)
    // 改为在 get_redis() 里按需连接

    // 3. 创建线程池
    ThreadPool pool(4);

    // 4. 创建 epoll
    int epoll_fd = epoll_create1(0);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    std::cout << "服务器启动, 监听端口 " << PORT << std::endl;

    struct epoll_event events[MAX_EVENTS];

    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                // 新连接
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) continue;

                fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);

                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
            } else {
                // 有数据
                char buffer[BUFFER_SIZE] = {0};
                int bytes_read = read(fd, buffer, BUFFER_SIZE - 1);
                if (bytes_read <= 0) {
                    close(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    continue;
                }

                std::string request(buffer, bytes_read);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);

                pool.enqueue([fd, request]() {
                    handle_request(fd, request);
                });
            }
        }
    }

    sqlite3_close(g_db);
    // ===== 修改 6：不再 redisFree(g_redis)，因为没有全局连接 =====
    close(server_fd);
    close(epoll_fd);
    return 0;
}