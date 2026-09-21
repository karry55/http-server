#pragma once

#include <iostream>

inline int g_passed = 0;
inline int g_failed = 0;

#define CHECK(cond)                                                  \
    do {                                                             \
        if (cond) { ++g_passed; }                                    \
        else {                                                       \
            ++g_failed;                                              \
            std::cerr << "  [FAIL] " << __FILE__ << ":" << __LINE__  \
                      << "  " #cond << "\n";                         \
        }                                                            \
    } while (0)

#define CHECK_EQ(a, b)                                               \
    do {                                                             \
        auto _va = (a); auto _vb = (b);                              \
        if (_va == _vb) { ++g_passed; }                              \
        else {                                                       \
            ++g_failed;                                              \
            std::cerr << "  [FAIL] " << __FILE__ << ":" << __LINE__  \
                      << "  " #a " == " #b "\n"                      \
                      << "         实际: " << _va << "\n"            \
                      << "         期望: " << _vb << "\n";           \
        }                                                            \
    } while (0)

// main 结尾调用。返回 0 = 全过，非 0 = 有失败（CTest 靠这个判断）
inline int test_summary(const char* name) {
    std::cout << name << ": " << g_passed << " 通过, " << g_failed << " 失败\n";
    return g_failed == 0 ? 0 : 1;
}