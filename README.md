# HTTP Todo Server（模块化版）

用 C++17 和 POSIX socket 从零实现的 HTTP Todo 服务端，**不使用任何 Web 框架**——
请求报文解析、路由分发、响应拼装全部手写。

本分支是**工程化版本**：功能拆成五个可独立测试的模块，用 CMake 构建。

> 六个递进式的单文件实现（从阻塞 `accept` 一路演进到 epoll + 线程池 + Redis 缓存，
> 含完整压测数据与瓶颈分析）在 **`single-file` 分支**。

## 架构

`main.cpp` 只负责组装，13 行：

```cpp
DB db("todo.db");
Cache cache;
Handler handler(db, cache);
Server server(8080, handler);
server.run();
```

| 模块 | 文件 | 职责 |
| --- | --- | --- |
| `Server` | `include/server.h` / `src/server.cpp` | socket、epoll、把连接派发给线程池 |
| `Handler` | `include/handler.h` / `src/handler.cpp` | 路由分发，返回 `(status, body)` |
| `DB` | `include/db.h` / `src/db.cpp` | SQLite 封装：增删改查 |
| `Cache` | `include/cache.h` / `src/cache.cpp` | Redis 封装：`get` / `set` / `del` |
| `ThreadPool` | `include/thread_pool.h` / `src/thread_pool.cpp` | 通用线程池（固定 4 线程） |

分层的实际收益在测试上：`Handler` 不碰 socket，所以能直接调用测试
（`tests/test_handler.cpp` 就是不经过网络、直接调 `handle()`）；`DB` 和 `Cache` 也各自能单独构造。

## 接口

监听 **8080** 端口，响应均为 `application/json` + `Connection: close`。

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| `GET` | `/` | 返回 `{"message": "home"}` |
| `GET` | `/status` | 健康检查，返回 `{"status": "ok"}` |
| `POST` | `/todo` | 新建待办，请求体即内容 |
| `GET` | `/todos` | 列出全部待办（走缓存） |
| `GET` | `/todo/<id>` | 查询单条，不存在返回 `404`（走缓存） |
| `PUT` | `/todo/<id>` | 更新单条内容 |
| `DELETE` | `/todo/<id>` | 删除单条 |

写操作成功返回 `{"status": "created" | "updated" | "deleted"}`；
路径参数非数字返回 `400` + `{"error": "invalid id"}`；未匹配路径返回 `404`。

### 缓存

| 键 | 内容 | TTL |
| --- | --- | --- |
| `todos_cache` | 整个列表序列化后的响应体 | 60 s |
| `todo_<id>` | 单条记录序列化后的响应体 | 60 s |

任何写操作（`POST` / `PUT` / `DELETE`）都会同时删掉对应条目的键和列表键。
缓存里存的是**已经拼好的 JSON body**，命中时直接把它当作响应返回，连 SQLite 都不进。

`Cache` 内部用 `thread_local` 保存 `redisContext`——hiredis 的连接不是线程安全的，
多个工作线程必须各用一条。连接是**按需惰性建立**的，不是启动时一次性连好。

## 编译与运行

```bash
sudo apt install build-essential cmake libsqlite3-dev libhiredis-dev

cmake -B build -S .
cmake --build build

redis-server --daemonize yes   # 缓存层需要
./build/http_server
```

试一下：

```bash
curl -X POST -d 'buy milk' http://127.0.0.1:8080/todo
curl http://127.0.0.1:8080/todos
curl http://127.0.0.1:8080/todo/1
curl -X PUT -d 'buy bread' http://127.0.0.1:8080/todo/1
curl -X DELETE http://127.0.0.1:8080/todo/1
```

### ⚠️ 必须在项目根目录运行

`todo.db` 是相对**当前工作目录**打开的，不是相对可执行文件的位置。
在别处启动会在那个目录新建一个空数据库，看起来就像"数据丢了"：

```bash
./build/http_server        # ✅ 在项目根目录，todo.db 落在这里
cd build && ./http_server  # ❌ 会在 build/ 另建一个空 todo.db
```

## 测试

四个模块各有冒烟测试，**没有接进 CMake**，需要单独编译：

```bash
g++ -std=c++17 -Iinclude -o build/test_db          tests/test_db.cpp          src/db.cpp -lsqlite3
g++ -std=c++17 -Iinclude -o build/test_cache       tests/test_cache.cpp       src/cache.cpp -lhiredis
g++ -std=c++17 -Iinclude -o build/test_handler     tests/test_handler.cpp     src/handler.cpp src/db.cpp src/cache.cpp -lsqlite3 -lhiredis
g++ -std=c++17 -Iinclude -o build/test_thread_pool tests/test_thread_pool.cpp src/thread_pool.cpp -pthread

./build/test_db && ./build/test_cache && ./build/test_handler && ./build/test_thread_pool
```

它们是**打印式**的——输出结果给人看，不做断言，所以不会自己报失败，得看输出判断：

- `test_db` —— 增删改查走一遍，打印每一步后的列表
- `test_cache` —— `set` / `get` / `del` 各一次
- `test_handler` —— 直接调 `Handler::handle()`，不经过 socket
- `test_thread_pool` —— 投 10 个任务，看 4 个线程怎么分

## 已知问题

- **fd 竞态（最需要修的一个）**：`Server::run()` 收到可读事件后，**没有先把 fd 从 epoll
  摘除**就丢进线程池，而读循环是在工作线程里做的（`src/server.cpp`）。
  `EPOLLET` 下如果请求分多个 TCP 段到达，同一个 fd 可能被投递两次任务，
  两个线程同时读同一个 fd——轻则数据错乱，重则 double close。
  修法是把读循环挪回 `Server::run()`，读完再 `EPOLL_CTL_DEL` 并入队
  （`single-file` 分支的 `http_server_et_loop.cpp` 就是这个写法）
- 所有工作线程共享同一个 `sqlite3*`。SQLite 默认 serialized 模式，数据库操作实际是串行的，
  加线程数换不来吞吐。要真并发需要开 WAL（`PRAGMA journal_mode=WAL`）+ 每线程一条连接
- `content` 未做 JSON 转义，直接拼进响应；含引号的待办会破坏 JSON
- 没有请求体大小上限、超时控制、`keep-alive`
- 线程池队列无上限，连接无背压
- 单元测试没有接进 CMake，也没有断言

## 仓库说明

`build/`、`*.o`、`*.out`、`*.db`、`*.log` 和 `.vscode/` 都不会入库。

其他分支：

- **`single-file`** —— 六个单文件版本，含完整压测数据（本分支只保留工程化实现）
- **`feature-*`** —— 开发中的功能，合回 `master` 后删除
