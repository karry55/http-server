#include "handler.h"
#include <cctype>
#include <iostream>

Handler::Handler(DB& db, Cache& cache, AI& ai)
    : db_(db), cache_(cache), ai_(ai) {}

std::pair<std::string, std::string> Handler::handle(
    const std::string& method,
    const std::string& path,
    const std::string& req_body) {

    std::string body;
    std::string status = "200 OK";

    if (method == "GET" && path == "/") {
        body = R"({"message": "home"})";
    } else if (method == "GET" && path == "/status") {
        body = R"({"status": "ok"})";
    } else if (method == "POST" && path == "/todo") {
        if (db_.addTodo(req_body)) {
            body = R"({"status": "created"})";
        } else {
            body = R"({"error": "insert failed"})";
            status = "500 Internal Server Error";
        }
        cache_.del("todos_cache");
    } else if (method == "GET" && path == "/todos") {
        std::string cached = cache_.get("todos_cache");
        if (!cached.empty()) {
            body = cached;
        } else {
            auto todos = db_.getTodos();
            body = "[";
            bool first = true;
            for (auto& t : todos) {
                if (!first) body += ",";
                body += R"({"id": )" + std::to_string(t.id) +
                        R"(, "content": ")" + t.content + R"("})";
                first = false;
            }
            body += "]";
            cache_.set("todos_cache", body, 60);
        }
    } else if (method == "GET" && path.rfind("/todo/", 0) == 0) {
        std::string id_str = path.substr(6);
        bool valid = !id_str.empty();
        for (char c : id_str) if (!isdigit((unsigned char)c)) valid = false;

        if (!valid) {
            body = R"({"error": "invalid id"})";
            status = "400 Bad Request";
        } else {
            int id = std::stoi(id_str);
            std::string key = "todo_" + std::to_string(id);
            std::string cached = cache_.get(key);
            if (!cached.empty()) {
                body = cached;
            } else {
                Todo t = db_.getTodo(id);
                if (t.id == -1) {
                    body = R"({"error": "not found"})";
                    status = "404 Not Found";
                } else {
                    body = R"({"id": )" + std::to_string(t.id) +
                           R"(, "content": ")" + t.content + R"("})";
                    cache_.set(key, body, 60);
                }
            }
        }
    } else if (method == "PUT" && path.rfind("/todo/", 0) == 0) {
        std::string id_str = path.substr(6);
        bool valid = !id_str.empty();
        for (char c : id_str) if (!isdigit((unsigned char)c)) valid = false;

        if (!valid) {
            body = R"({"error": "invalid id"})";
            status = "400 Bad Request";
        } else {
            int id = std::stoi(id_str);
            if (db_.updateTodo(id, req_body)) {
                body = R"({"status": "updated"})";
            } else {
                body = R"({"error": "update failed"})";
                status = "500 Internal Server Error";
            }
            cache_.del("todo_" + std::to_string(id));
            cache_.del("todos_cache");
        }
    } else if (method == "DELETE" && path.rfind("/todo/", 0) == 0) {
        std::string id_str = path.substr(6);
        bool valid = !id_str.empty();
        for (char c : id_str) if (!isdigit((unsigned char)c)) valid = false;

        if (!valid) {
            body = R"({"error": "invalid id"})";
            status = "400 Bad Request";
        } else {
            int id = std::stoi(id_str);
            if (db_.deleteTodo(id)) {
                body = R"({"status": "deleted"})";
            } else {
                body = R"({"error": "delete failed"})";
                status = "500 Internal Server Error";
            }
            cache_.del("todo_" + std::to_string(id));
            cache_.del("todos_cache");
        }
    } else if (method == "POST" && path == "/todo/classify") {
    std::string category = ai_.Classify(req_body);
    body = R"({"category": ")" + category + R"("})";
    }else {
        body = R"({"error": "not found"})";
        status = "404 Not Found";
    }

    return {status, body};
}
