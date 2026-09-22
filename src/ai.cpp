#include "ai.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total = size * nmemb;
    output->append((char*)contents, total);
    return total;
}

AI::AI() {
    api_key_ = LoadEnv("DEEPSEEK_API_KEY");
    if (api_key_.empty()) {
        std::cerr << "请检查 .env 文件中的 DEEPSEEK_API_KEY" << std::endl;
    }
}

AI::~AI() {}

std::string AI::LoadEnv(const std::string& key) {
    std::ifstream file(".env");
    if (!file.is_open()) return "";

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);
            while (!k.empty() && k.back() == ' ') k.pop_back();
            while (!v.empty() && v.front() == ' ') v.erase(0, 1);
            if (k == key) return v;
        }
    }
    return "";
}

std::string AI::CallDeepSeek(const std::string& prompt) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        json request_body = {
            {"model", "deepseek-chat"},
            {"messages", {
                {{"role", "user"}, {"content", prompt}}
            }}
        };

        std::string body_str = request_body.dump();

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());

        curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/chat/completions");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl 错误: " << curl_easy_strerror(res) << std::endl;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return response;
}

std::string AI::Classify(const std::string& content) {
    std::string prompt = "把这句话分类（购物/工作/学习/其他），只返回分类结果：" + content;
    std::string response = CallDeepSeek(prompt);

      std::cout << "AI 原始响应: " << response << std::endl;  

    try {
        json j = json::parse(response);
        return j["choices"][0]["message"]["content"];
    } catch (const std::exception& e) {
        std::cerr << "解析错误: " << e.what() << std::endl;
        return "其他";
    }
}