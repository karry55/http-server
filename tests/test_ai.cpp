#include "ai.h"
#include "check.h"
#include <iostream>

int main() {
    AI ai;

    // 测试分类
    std::string category = ai.Classify("买牛奶");
    std::cout << "买牛奶 -> " << category << std::endl;
    CHECK(!category.empty());

    std::string category2 = ai.Classify("学习C++");
    std::cout << "学习C++ -> " << category2 << std::endl;
    CHECK(!category2.empty());

    std::string category3 = ai.Classify("写代码");
    std::cout << "写代码 -> " << category3 << std::endl;
    CHECK(!category3.empty());

    return test_summary("test_ai");
}