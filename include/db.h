#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>

struct Todo {
    int id;
    std::string content;
};

class DB {
public:
    DB(const std::string& path);
    ~DB();

    bool addTodo(const std::string& content);
    std::vector<Todo> getTodos();
    Todo getTodo(int id);          // 找不到时 id = -1
    bool updateTodo(int id, const std::string& content);
    bool deleteTodo(int id);

private:
    sqlite3* db_;
};
