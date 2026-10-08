cmake_minimum_required(VERSION 3.10)
project(http_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 打开编译器警告：让它帮我们找出可疑代码。
#   -Wall    常用警告
#   -Wextra  更多警告
#   -Wshadow 变量遮蔽（内层同名变量挡住外层，很隐蔽的坑）
# 注意先不要加 -Werror，否则任何警告都会导致编译失败，初学者容易卡住。
add_compile_options(-Wall -Wextra -Wshadow)

include_directories(include)

set(SOURCES
    main.cpp
    src/server.cpp
    src/handler.cpp
    src/db.cpp
    src/cache.cpp
    src/thread_pool.cpp
    src/ai.cpp
)

add_executable(http_server ${SOURCES})
target_link_libraries(http_server sqlite3 hiredis pthread curl)

enable_testing()

function(add_unit_test name)
    add_executable(${name} tests/${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE include tests)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_db          src/db.cpp)
target_link_libraries(test_db sqlite3)

add_unit_test(test_thread_pool src/thread_pool.cpp)
target_link_libraries(test_thread_pool pthread)

add_unit_test(test_cache       src/cache.cpp)
target_link_libraries(test_cache hiredis)

add_unit_test(test_handler     src/handler.cpp src/db.cpp src/cache.cpp src/ai.cpp)
target_link_libraries(test_handler sqlite3 hiredis curl)