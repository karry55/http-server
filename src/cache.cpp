#include "cache.h"
#include <hiredis/hiredis.h>

// 每个线程独立连接
static thread_local redisContext* t_redis = nullptr;

static redisContext* get_redis() {
    if (t_redis == nullptr || t_redis->err) {
        t_redis = redisConnect("127.0.0.1", 6379);
    }
    return t_redis;
}

Cache::Cache() {
    // 第一次调用 get_redis 时连接
    get_redis();
}

Cache::~Cache() {
    // thread_local 连接由线程结束时自动释放
}

std::string Cache::get(const std::string& key) {
    redisReply* reply = (redisReply*)redisCommand(get_redis(), "GET %s", key.c_str());
    std::string result;
    if (reply && reply->type == REDIS_REPLY_STRING) {
        result = reply->str;
    }
    if (reply) freeReplyObject(reply);
    return result;
}

void Cache::set(const std::string& key, const std::string& value, int ttl) {
    redisCommand(get_redis(), "SETEX %s %d %b", key.c_str(), ttl, value.c_str(), value.size());
}

void Cache::del(const std::string& key) {
    redisCommand(get_redis(), "DEL %s", key.c_str());
}
