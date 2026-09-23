#pragma once
#include <string>

class AI {
public:
    AI();
    ~AI();
    std::string Classify(const std::string& content);
    std::string Summarize(const std::string& todos);   
    std::string Prioritize(const std::string& content); 
private:
    std::string api_key_;
    std::string CallDeepSeek(const std::string& prompt);
    std::string LoadEnv(const std::string& key);
};