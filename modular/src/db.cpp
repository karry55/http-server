#include "db.h"
#include <iostream>

DB::DB(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "无法打开数据库: " << sqlite3_errmsg(db_) << std::endl;
        db_ = nullptr;
        return;
    }
    const char* sql =
        "CREATE TABLE IF NOT EXISTS todos ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "content TEXT NOT NULL"
        ");";
    sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
}

DB::~DB() {
    if (db_) sqlite3_close(db_);
}

bool DB::addTodo(const std::string& content) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, "INSERT INTO todos (content) VALUES (?);", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, content.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<Todo> DB::getTodos() {
    std::vector<Todo> result;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, "SELECT id, content FROM todos;", -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Todo t;
        t.id = sqlite3_column_int(stmt, 0);
        const char* content = (const char*)sqlite3_column_text(stmt, 1);
        t.content = content ? content : "";
        result.push_back(t);
    }
    sqlite3_finalize(stmt);
    return result;
}

Todo DB::getTodo(int id) {
    Todo t{-1, ""};
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, "SELECT id, content FROM todos WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) {
        return t;
    }
    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        t.id = sqlite3_column_int(stmt, 0);
        const char* content = (const char*)sqlite3_column_text(stmt, 1);
        t.content = content ? content : "";
    }
    sqlite3_finalize(stmt);
    return t;
}

bool DB::updateTodo(int id, const std::string& content) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, "UPDATE todos SET content = ? WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, id);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DB::deleteTodo(int id) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, "DELETE FROM todos WHERE id = ?;", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}
