cmake_minimum_required(VERSION 3.10)
project(http_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 头文件路径
include_directories(include)

# 源文件
set(SOURCES
    main.cpp
    src/server.cpp
    src/handler.cpp
    src/db.cpp
    src/cache.cpp
    src/thread_pool.cpp
    src/ai.cpp
)

# 可执行文件
add_executable(http_server ${SOURCES})

# 链接库
target_link_libraries(http_server
    sqlite3
    hiredis
    pthread
    curl
)

# ==================== 测试 ====================
enable_testing()          # 必须写在顶层，否则 CTest 认不到

# 辅助函数，省得每个测试重复写三行
function(add_unit_test name)
    add_executable(${name} tests/${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE include tests)   # 让 check.h 能被找到
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_db          src/db.cpp)
target_link_libraries(test_db sqlite3)

add_unit_test(test_thread_pool src/thread_pool.cpp)
target_link_libraries(test_thread_pool pthread)

add_unit_test(test_cache       src/cache.cpp)
target_link_libraries(test_cache hiredis)

add_unit_test(test_handler     src/handler.cpp src/db.cpp src/cache.cpp)
target_link_libraries(test_handler sqlite3 hiredis)