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
)

# 可执行文件
add_executable(http_server ${SOURCES})

# 链接库
target_link_libraries(http_server
    sqlite3
    hiredis
    pthread
)
