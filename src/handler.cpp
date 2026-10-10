#include "handler.h"

#include <cctype>
#include <iostream>

namespace {
// 模型回复常带结尾换行（"工作\n"），直接拼进 JSON 会让前端拿到多余的空白。
// 注意只去首尾，中间的空格/换行要保留（比如总结可能是多行）。
void trim(std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    s = s.substr(b, e - b);
}
// 把文本转义成能安全放进 JSON 双引号里的形式。
// JSON 规定字符串里的 " 和 \ 必须写成 \" 和 \\，换行、制表符等控制字符也要转义。
// 不做这一步的话，待办里只要有一个双引号，整个响应就不是合法 JSON 了，
// 前端 JSON.parse 会直接失败——一条坏数据会把整个列表打挂。
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (c < 0x20) {
                    // 其他控制字符写成 \u00XX
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += static_cast<char>(c);  // 普通字节（含中文）原样保留
                }
        }
    }
    return out;
}

// 把字符串安全地转成 int。转不了就返回 false，绝不抛异常。
bool parse_id(const std::string& s, int& out) {
    if (s.empty())
        return false;
    try {
        size_t pos = 0;
        long v = std::stol(s, &pos);  // stol 能装更大的数
        if (pos != s.size())
            return false;  // 尾巴上有多余字符
        if (v < 1 || v > 2147483647L)
            return false;  // int 的范围是 1 ~ 2147483647
        out = static_cast<int>(v);
        return true;
    } catch (const std::exception&) {
        return false;  // 连 stol 都装不下 → 视为非法
    }
}
}  // namespace

Handler::Handler(DB& db, Cache& cache, AI& ai) : db_(db), cache_(cache), ai_(ai) {
}

std::pair<std::string, std::string> Handler::handle(const std::string& method,
                                                    const std::string& path,
                                                    const std::string& req_body) {
    std::string body;
    std::string status = "200 OK";

    // CORS 预检：浏览器发 PUT/DELETE 或带自定义头的请求前会先发 OPTIONS。
    // 这里不区分路径，一律放行，真正的鉴权/路由交给下面的分支。
    // 204 不能带 body，响应头由 Server 统一补（见 src/server.cpp）。
    if (method == "OPTIONS") {
        return {"204 No Content", ""};
    }

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
                if (!first)
                    body += ",";
                body += R"({"id": )" + std::to_string(t.id) + R"(, "content": ")" +
                        json_escape(t.content) + R"("})";
                first = false;
            }
            body += "]";
            cache_.set("todos_cache", body, 60);
        }
    } else if (method == "GET" && path.rfind("/todo/", 0) == 0) {
        std::string id_str = path.substr(6);
        int id = 0;
        if (!parse_id(id_str, id)) {
            body = R"({"error": "invalid id"})";
            status = "400 Bad Request";
        } else {
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
                    body = R"({"id": )" + std::to_string(t.id) + R"(, "content": ")" +
                           json_escape(t.content) + R"("})";
                    cache_.set(key, body, 60);
                }
            }
        }
    } else if (method == "PUT" && path.rfind("/todo/", 0) == 0) {
        std::string id_str = path.substr(6);
        int id = 0;
        if (!parse_id(id_str, id)) {
            body = R"({"error": "invalid id"})";
            status = "400 Bad Request";
        } else {
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
        int id = 0;
        if (!parse_id(id_str, id)) {
            body = R"({"error": "invalid id"})";
            status = "400 Bad Request";
        } else {
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
        trim(category);
        body = R"({"category": ")" + json_escape(category) + R"("})";
    } else if (method == "POST" && path == "/todo/summarize") {
        std::string summary = ai_.Summarize(req_body);
        trim(summary);
        body = R"({"summary": ")" + json_escape(summary) + R"("})";
    } else if (method == "POST" && path == "/todo/prioritize") {
        std::string priority = ai_.Prioritize(req_body);
        trim(priority);
        body = R"({"priority": ")" + json_escape(priority) + R"("})";
    } else if (method == "POST" && path == "/todo/split") {
        std::string subtasks = ai_.Split(req_body);
        body = R"({"subtasks": ")" + subtasks + R"("})";
    } else if (method == "POST" && path == "/chat") {
        std::string reply = ai_.Chat(req_body);
        body = R"({"reply": ")" + reply + R"("})";
    } else {
        body = R"({"error": "not found"})";
        status = "404 Not Found";
    }

    return {status, body};
}
