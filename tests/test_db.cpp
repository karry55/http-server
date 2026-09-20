#include "db.h"
#include <iostream>

int main() {
    DB db("test.db");

    // 添加
    db.addTodo("buy milk");
    db.addTodo("learn C++");

    // 查询所有
    auto todos = db.getTodos();
    std::cout << "所有待办:" << std::endl;
    for (auto& t : todos) {
        std::cout << "  " << t.id << ": " << t.content << std::endl;
    }

    // 查询单条
    auto t = db.getTodo(1);
    std::cout << "id=1: " << t.content << std::endl;

    // 更新
    db.updateTodo(1, "buy bread");

    // 删除
    db.deleteTodo(2);

    // 再查询
    todos = db.getTodos();
    std::cout << "更新+删除后:" << std::endl;
    for (auto& t : todos) {
        std::cout << "  " << t.id << ": " << t.content << std::endl;
    }

    return 0;
}
