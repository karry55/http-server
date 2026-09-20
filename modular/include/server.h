#pragma once

#include <string>
#include "thread_pool.h"
#include "handler.h"

class Server {
public:
    Server(int port, Handler& handler);
    ~Server();

    void run();

private:
    int port_;
    int server_fd_;
    int epoll_fd_;
    Handler& handler_;
    ThreadPool pool_;

    void handle_client(int fd);
};
