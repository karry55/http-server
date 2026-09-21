#include "db.h"
#include "check.h"

int main() {
    DB db(":memory:");          // ← 内存数据库，测试间互不干扰

    CHECK_EQ(db.getTodos().size(), 0u);          // 空库

    CHECK(db.addTodo("buy milk"));               // 增
    CHECK(db.addTodo("learn C++"));
    CHECK_EQ(db.getTodos().size(), 2u);

    CHECK_EQ(db.getTodo(1).content, std::string("buy milk"));   // 查
    CHECK_EQ(db.getTodo(999).id, -1);                            // 查不到

    CHECK(db.updateTodo(1, "buy bread"));        // 改
    CHECK_EQ(db.getTodo(1).content, std::string("buy bread"));

    CHECK(db.deleteTodo(2));                     // 删
    CHECK_EQ(db.getTodos().size(), 1u);

    return test_summary("test_db");
}