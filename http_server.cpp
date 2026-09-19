#include <iostream>
#include <string>
#include <cstring>
#include <cctype>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sqlite3.h>

#define PORT 8080
#define BUFFER_SIZE 4096

int main() {
    // ========== 1. 创建 socket ==========
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    // ========== 2. 端口复用 ==========
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // ========== 3. 绑定地址 ==========
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    // ========== 4. 监听 ==========
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }

    // ========== 5. 初始化数据库 ==========
    sqlite3* db;
    if (sqlite3_open("todo.db", &db) != SQLITE_OK) {
        std::cerr << "无法打开数据库: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }

    const char* create_sql =
        "CREATE TABLE IF NOT EXISTS todos ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "content TEXT NOT NULL"
        ");";

    char* err_msg = nullptr;
    if (sqlite3_exec(db, create_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "建表失败: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return 1;
    }

    std::cout << "服务器启动, 监听端口 " << PORT << std::endl;
    std::cout << "访问 http://127.0.0.1:" << PORT << std::endl;

    // ========== 6. 循环接收连接 ==========
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        // ---------- 读取请求 ----------
        char buffer[BUFFER_SIZE] = {0};
        int bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            close(client_fd);
            continue;
        }

        // ---------- 解析请求行 ----------
        std::string request(buffer, bytes_read);
        std::string method, path;
        size_t pos1 = request.find(' ');
        size_t pos2 = request.find(' ', pos1 + 1);
        if (pos1 != std::string::npos && pos2 != std::string::npos) {
            method = request.substr(0, pos1);
            path   = request.substr(pos1 + 1, pos2 - pos1 - 1);
        }

        // 去掉查询参数 ?...
        size_t q = path.find('?');
        if (q != std::string::npos) path = path.substr(0, q);

        std::cout << "方法: " << method << ", 路径: " << path << std::endl;

        // ---------- 解析 POST body ----------
        size_t body_pos = request.find("\r\n\r\n");
        std::string req_body;
        if (body_pos != std::string::npos) {
            req_body = request.substr(body_pos + 4);
        }

        // ---------- 路由 ----------
        std::string body;
        std::string status = "200 OK";

        if (method == "POST" && path == "/echo") {
            body = R"({"echo": ")" + req_body + R"("})";
        }
        else if (method == "GET" && path == "/") {
            body = R"({"message": "home"})";
        }
        else if (method == "GET" && path == "/status") {
            body = R"({"status": "ok"})";
        }
        else if (method == "POST" && path == "/todo") {
            // 插入数据（参数绑定）
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db,
                    "INSERT INTO todos (content) VALUES (?);",
                    -1, &stmt, nullptr) != SQLITE_OK) {
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
        }
        else if (method == "GET" && path == "/todos") {
            // 查询所有
            sqlite3_stmt* stmt;
            if (sqlite3_prepare_v2(db, "SELECT id, content FROM todos;",
                                   -1, &stmt, nullptr) != SQLITE_OK) {
                body = R"({"error": "query failed"})";
                status = "500 Internal Server Error";
            } else {
                body = "[";
                bool first = true;
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    int id = sqlite3_column_int(stmt, 0);
                    const char* content = (const char*)sqlite3_column_text(stmt, 1);
                    if (!first) body += ",";
                    body += R"({"id": )" + std::to_string(id) +
                            R"(, "content": ")" + (content ? content : "") + R"("})";
                    first = false;
                }
                body += "]";
                sqlite3_finalize(stmt);
            }
        }
        else if (method == "DELETE" && path.rfind("/todo/", 0) == 0) {
            // 删除（参数校验 + 绑定）
            std::string id_str = path.substr(6);
            bool valid = !id_str.empty();
            for (char c : id_str) {
                if (!isdigit((unsigned char)c)) { valid = false; break; }
            }

            if (!valid) {
                body = R"({"error": "invalid id"})";
                status = "400 Bad Request";
            } else {
                int id = std::stoi(id_str);

                sqlite3_stmt* stmt;
                if (sqlite3_prepare_v2(db,
                        "DELETE FROM todos WHERE id = ?;",
                        -1, &stmt, nullptr) != SQLITE_OK) {
                    body = R"({"error": "prepare failed"})";
                    status = "500 Internal Server Error";
                } else {
                    sqlite3_bind_int(stmt, 1, id);
                    if (sqlite3_step(stmt) != SQLITE_DONE) {
                        body = R"({"error": "delete failed"})";
                        status = "500 Internal Server Error";
                    } else {
                        body = R"({"status": "deleted"})";
                    }
                    sqlite3_finalize(stmt);
                }
            }
        }
        else {
            body = R"({"error": "not found"})";
            status = "404 Not Found";
        }

        // ---------- 构造响应 ----------
        std::string response =
            "HTTP/1.1 " + status + "\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n"
            "\r\n" + body;

        // ---------- 发送响应 ----------
        write(client_fd, response.c_str(), response.size());

        // ---------- 关闭连接 ----------
        close(client_fd);
    }

    sqlite3_close(db);
    close(server_fd);
    return 0;
}