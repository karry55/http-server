#include "server.h"
#include <iostream>
#include <cstring>
#include <cctype>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cerrno>

#define BUFFER_SIZE 4096
#define MAX_EVENTS 1024

namespace {

// 取出请求头里的 Origin（大小写不敏感，任意一行）。找不到就返回空串。
std::string get_origin(const std::string& request) {
    size_t head_end = request.find("\r\n\r\n");
    std::string head = (head_end == std::string::npos) ? request
                                                       : request.substr(0, head_end);

    size_t pos = head.find("\r\n");
    if (pos == std::string::npos) return "";   // 只有请求行，没有头
    pos += 2;

    while (pos < head.size()) {
        size_t eol = head.find("\r\n", pos);
        if (eol == std::string::npos) eol = head.size();

        size_t colon = head.find(':', pos);
        if (colon != std::string::npos && colon < eol) {
            std::string name = head.substr(pos, colon - pos);
            for (char& c : name) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (name == "origin") {
                size_t v = colon + 1;
                while (v < eol && (head[v] == ' ' || head[v] == '\t')) ++v;
                return head.substr(v, eol - v);
            }
        }
        pos = eol + 2;
    }
    return "";
}

// 只回显看起来正常的 Origin。
// 直接拼接未校验的头值会被 CRLF 注入（攻击者塞进额外响应头），所以带控制字符就退回 "*"。
std::string safe_origin(const std::string& origin) {
    if (origin.empty()) return "*";
    for (unsigned char c : origin) {
        if (c < 0x20 || c == 0x7f) return "*";
    }
    return origin;
}

}  // namespace

Server::Server(int port, Handler& handler)
    : port_(port), server_fd_(-1), epoll_fd_(-1), handler_(handler), pool_(4) {}

Server::~Server() {
    if (server_fd_ >= 0) close(server_fd_);
    if (epoll_fd_ >= 0) close(epoll_fd_);
}

void Server::run() {
    // 1. 创建 socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) { perror("socket"); return; }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind"); return;
    }
    if (listen(server_fd_, 10) < 0) { perror("listen"); return; }

    // 2. 创建 epoll
    epoll_fd_ = epoll_create1(0);
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev);

    std::cout << "服务器启动, 监听端口 " << port_ << std::endl;

    // 3. 主循环
    struct epoll_event events[MAX_EVENTS];
    while (1) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == server_fd_) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) continue;

                fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev);
            } else {
                pool_.enqueue([this, fd]() { handle_client(fd); });
            }
        }
    }
}

void Server::handle_client(int fd) {
    // 循环读
    std::string request;
    char buffer[BUFFER_SIZE] = {0};
    while (true) {
        int bytes_read = read(fd, buffer, BUFFER_SIZE - 1);
        if (bytes_read > 0) {
            request.append(buffer, bytes_read);
        } else if (bytes_read == 0) {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
                return;
            }
        }
    }

    if (request.empty()) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        return;
    }

    // 从 epoll 移除
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

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

    // 解析 body
    size_t body_pos = request.find("\r\n\r\n");
    std::string req_body;
    if (body_pos != std::string::npos) {
        req_body = request.substr(body_pos + 4);
    }

    // 交给 handler
    auto [status, body] = handler_.handle(method, path, req_body);

    // 构造响应
    // 允许跨域：页面是 file:// 打开的，Origin 为 "null"，必须回显而不是只发 "*"。
    // 回显具体 Origin（而非 "*"）也方便以后要带 Cookie 时不用再改。
    std::string response =
        "HTTP/1.1 " + status + "\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: " + safe_origin(get_origin(request)) + "\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Vary: Origin\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    write(fd, response.c_str(), response.size());
    close(fd);
}
