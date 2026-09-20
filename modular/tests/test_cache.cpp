#include "cache.h"
#include <iostream>

int main() {
    Cache cache;

    // 设置
    cache.set("test_key", "hello", 60);

    // 获取
    std::string value = cache.get("test_key");
    std::cout << "获取: " << value << std::endl;

    // 删除
    cache.del("test_key");

    // 再获取
    value = cache.get("test_key");
    std::cout << "删除后: " << (value.empty() ? "(空)" : value) << std::endl;

    return 0;
}
