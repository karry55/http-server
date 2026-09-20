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
    std::string response =
        "HTTP/1.1 " + status + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;

    write(fd, response.c_str(), response.size());
    close(fd);
}
