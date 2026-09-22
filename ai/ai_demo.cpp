#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>

using json = nlohmann::json;

// curl 回调
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total = size * nmemb;
    output->append((char*)contents, total);
    return total;
}

std::string CallDeepSeek(const std::string& prompt, const std::string& api_key) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (curl) {
        // 构造请求体
        json request_body = {
            {"model", "deepseek-chat"},
            {"messages", {
                {{"role", "user"}, {"content", prompt}}
            }}
        };

        std::string body_str = request_body.dump();

        // 设置请求头
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());

        // 设置 curl
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.deepseek.com/chat/completions");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // 发请求
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "curl 错误: " << curl_easy_strerror(res) << std::endl;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }

    return response;
}

std::string load_env(const std::string& key) {
    std::ifstream file(".env");
    if (!file.is_open()) return "";

    std::string line;
    while (std::getline(file, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;

        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string k = line.substr(0, pos);
            std::string v = line.substr(pos + 1);

            // 去掉空格
            while (!k.empty() && k.back() == ' ') k.pop_back();
            while (!v.empty() && v.front() == ' ') v.erase(0, 1);

            if (k == key) return v;
        }
    }
    return "";
}

int main() {
     std::string api_key = load_env("DEEPSEEK_API_KEY");
    if (api_key.empty()) {
        std::cerr << "请检查 .env 文件中的 DEEPSEEK_API_KEY" << std::endl;
        return 1;
    }
    std::string prompt = "把这句话分类（购物/工作/学习/其他）：买牛奶";

    std::string response = CallDeepSeek(prompt, api_key);
    std::cout << "响应: " << response << std::endl;

    // 解析响应
    try {
        json j = json::parse(response);
        std::string content = j["choices"][0]["message"]["content"];
        std::cout << "AI 回复: " << content << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "解析错误: " << e.what() << std::endl;
    }

    return 0;
}