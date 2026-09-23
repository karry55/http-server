# HTTP Todo Server（模块化版）

用 C++17 和 POSIX socket 从零实现的 HTTP Todo 服务端，**不使用任何 Web 框架**——
请求报文解析、路由分发、响应拼装全部手写。

本分支是**工程化版本**：功能拆成六个可独立构造、可单独测试的模块，用 CMake 构建，
并接入了 AI 分类能力。

> 六个递进式的单文件实现（从阻塞 `accept` 一路演进到 epoll + 线程池 + Redis 缓存，
> 含完整压测数据与瓶颈分析）在 **`single-file` 分支**（GitHub 上是同名分支）。

## 目录结构

```
.
├── CMakeLists.txt
├── main.cpp                 组装五个对象，15 行
├── .env                     真实密钥，**不入库**（见下方「AI 集成」）
├── .env.example             模板，只有键名，可以入库
├── include/                 接口声明
│   ├── server.h               socket + epoll + 线程池调度
│   ├── handler.h              路由分发，返回 (status, body)
│   ├── db.h                   SQLite 封装
│   ├── cache.h                Redis 封装
│   ├── ai.h                   DeepSeek 调用封装
│   └── thread_pool.h          通用线程池
├── src/                     对应实现
├── tests/                   单元测试
│   ├── check.h                手写断言宏（CHECK / CHECK_EQ / test_summary）
│   └── test_*.cpp             四个模块各自的测试
└── demo/
    └── ai_demo.cpp          独立的 AI 调用示例（不依赖服务器）
```

## 架构

`main.cpp` 只负责组装：

```cpp
DB db("todo.db");
Cache cache;
AI ai;
Handler handler(db, cache, ai);
Server server(8080, handler);
server.run();
```

| 模块 | 文件 | 职责 |
| --- | --- | --- |
| `Server` | `include/server.h` · `src/server.cpp` | socket、epoll、把连接派发给线程池 |
| `Handler` | `include/handler.h` · `src/handler.cpp` | 路由分发，返回 `(status, body)` |
| `DB` | `include/db.h` · `src/db.cpp` | SQLite 封装：`addTodo` / `getTodos` / `getTodo` / `updateTodo` / `deleteTodo` |
| `Cache` | `include/cache.h` · `src/cache.cpp` | Redis 封装：`get` / `set` / `del` |
| `AI` | `include/ai.h` · `src/ai.cpp` | 调 DeepSeek API 做文本分类 |
| `ThreadPool` | `include/thread_pool.h` · `src/thread_pool.cpp` | 通用线程池，固定 4 线程 |

分层的实际收益主要体现在测试上：`Handler` 不碰 socket，所以能直接调用（`tests/test_handler.cpp`
就是不经过网络、直接调 `handle()`）；`DB` 和 `Cache` 也各自能单独构造，不依赖服务器先跑起来。

## 接口

监听 **8080** 端口，响应均为 `application/json` + `Connection: close`。

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| `GET` | `/` | 返回 `{"message": "home"}` |
| `GET` | `/status` | 健康检查，返回 `{"status": "ok"}` |
| `POST` | `/todo` | 新建待办，请求体即内容，返回 `{"status": "created"}` |
| `GET` | `/todos` | 列出全部待办（走缓存） |
| `GET` | `/todo/<id>` | 查询单条，不存在返回 `404`（走缓存） |
| `PUT` | `/todo/<id>` | 更新单条内容，返回 `{"status": "updated"}` |
| `DELETE` | `/todo/<id>` | 删除单条，返回 `{"status": "deleted"}` |
| `POST` | `/todo/classify` | **调 AI 分类**，请求体是待分类的句子，返回 `{"category": "..."}` |

路径参数非数字返回 `400` + `{"error": "invalid id"}`；
未匹配的路径返回 `404` + `{"error": "not found"}`。

## 缓存

| 键 | 内容 | TTL |
| --- | --- | --- |
| `todos_cache` | 整个列表序列化后的响应体 | 60 s |
| `todo_<id>` | 单条记录序列化后的响应体 | 60 s |

任何写操作（`POST` / `PUT` / `DELETE`）都会同时删掉对应条目的键和列表键。

缓存里存的是**已经拼好的 JSON body**——命中时直接把它当响应返回，既不进 SQLite，
也不需要重新做字符串拼接。

`Cache` 内部用 `thread_local` 保存 `redisContext`：hiredis 的连接不是线程安全的，
多个工作线程必须各用一条，共享一条得自己加锁，否则会直接数据错乱。
连接是**按需惰性建立**的，不是启动时一次性连好。

## AI 集成

`AI` 类把一句话分类成 **购物 / 工作 / 学习 / 其他** 四类之一。

### 调用链

```
POST /todo/classify  →  Handler::handle()  →  AI::Classify()
                                                    ↓
                                              AI::CallDeepSeek()
                                                    ↓
                                        libcurl → api.deepseek.com
                                                    ↓
                                        nlohmann/json 解析响应
```

`Classify()` 的提示词是：

```
把这句话分类（购物/工作/学习/其他），只返回分类结果：<你的输入>
```

模型返回的内容直接作为 `category` 返回。**解析失败时兜底返回 `其他`**，
所以 AI 挂掉不会让服务崩掉，只是分类不准。

### 依赖

| 依赖 | 用途 | 安装 |
| --- | --- | --- |
| **libcurl** | 发 HTTPS 请求 | `libcurl4-openssl-dev` |
| **nlohmann/json** | 解析 JSON 响应 | `nlohmann-json3-dev`（纯头文件库） |

```bash
sudo apt install libcurl4-openssl-dev nlohmann-json3-dev
```

### 配置 API Key

**先复制模板，再填真实值：**

```bash
cp .env.example .env
# 然后编辑 .env，把 sk-your-key-here 换成真 key
```

`.env` 的内容就一行：

```
DEEPSEEK_API_KEY=sk-你的真实key
```

> ## ⚠️ `.env` 绝对不能提交进 Git
>
> 这个仓库已经踩过一次坑：`ai/.env` 曾被提交并推送到 Gitee（公开仓库），
> **导致 API Key 泄露、必须吊销重发**。GitHub 的推送保护拦下了，Gitee 没有。
>
> 现在的 `.gitignore` 已经包含下面三行，**不要删掉**：
>
> ```gitignore
> .env
> .env.*
> !.env.example
> ```
>
> **记住：任何平台的自动扫描都只是兜底，不能代替 `.gitignore`。**

### 独立运行 demo

`demo/ai_demo.cpp` 是一个不依赖服务器的示例，直接调 API 并打印原始响应：

```bash
cd demo
g++ -std=c++17 -o ai_demo ai_demo.cpp -lcurl
./ai_demo
```

⚠️ 它读的是**当前工作目录**下的 `.env`，所以在 `demo/` 里跑需要 `demo/.env`。
**更推荐从项目根目录跑，只在根目录放一份 `.env`**（见下方「已知问题」）。

## 编译与运行

### 依赖

```bash
sudo apt install build-essential cmake \
                 libsqlite3-dev libhiredis-dev \
                 libcurl4-openssl-dev nlohmann-json3-dev \
                 redis-server
```

### 编译

```bash
cmake -B build -S .
cmake --build build
```

### 运行

```bash
redis-server --daemonize yes   # 缓存层需要
./build/http_server
```

### 试一下

```bash
# 待办 CRUD
curl -X POST -d 'buy milk' http://127.0.0.1:8080/todo
curl http://127.0.0.1:8080/todos
curl http://127.0.0.1:8080/todo/1
curl -X PUT -d 'buy bread' http://127.0.0.1:8080/todo/1
curl -X DELETE http://127.0.0.1:8080/todo/1

# AI 分类
curl -X POST -d '明天要交周报' http://127.0.0.1:8080/todo/classify
# {"category": "工作"}
```

### ⚠️ 必须在项目根目录运行

`todo.db` 和 `.env` 都是相对**当前工作目录**打开/读取的，不是相对可执行文件的位置。
在别处启动会在那个目录新建一个空数据库、并且读不到 API Key：

```bash
./build/http_server        # ✅ 在项目根目录，todo.db 和 .env 都能找到
cd build && ./http_server  # ❌ 会在 build/ 另建空 todo.db，也找不到 .env
```

## 测试

四个模块的单元测试**已经接进 CMake / CTest**，用 `tests/check.h` 里的断言宏判断对错，
不需要人眼看输出。

### 编译并运行全部测试

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

输出：

```
    Start 1: test_db
1/4 Test #1: test_db ..........................   Passed    0.03 sec
...
100% tests passed, 0 tests failed out of 4
```

`--output-on-failure` 表示只有失败的用例才把输出贴出来。

### 只跑其中几个

```bash
ctest --test-dir build -R db                    # 只跑名字含 db 的
ctest --test-dir build -R "test_(db|cache)"     # 跑 db 和 cache
ctest --test-dir build -N                       # 只列出不运行
```

### 在 VS Code 里单独跑某一个

- **CMake Tools**：`Ctrl+Shift+P` → `CMake: Run Tests`
- **C++ TestMate 扩展**：左侧测试树里每个用例旁边都有 ▶，点一下跑一个
- **单独调试某个测试**：在测试树里右键 → `Debug Test`，能在断点处停下

### 各测试在测什么

| 测试 | 覆盖内容 | 需要 Redis |
| --- | --- | --- |
| `test_db` | 空库、增、查、查不到（返回 `-1`）、改、删 | ❌（用 `:memory:`） |
| `test_cache` | `set` / `get` / `del`，验证删除后读回为空 | ✅ |
| `test_handler` | 直接调 `Handler::handle()`，走 `GET /`、`POST /todo`、`GET /todos` | ✅ |
| `test_thread_pool` | 投 10 个任务，观察 4 个线程如何分配 | ❌ |

**注意**：`test_db` 用的是 `DB db(":memory:")`——内存数据库，每次都是全新的，
测试之间不会互相污染，也不会在磁盘上留垃圾文件。这是单元测试的推荐做法。

### 断言宏

`tests/check.h` 提供了三个工具：

| 宏 / 函数 | 作用 |
| --- | --- |
| `CHECK(cond)` | 条件为假时记一次失败，打印出错的文件与行号，**继续往下跑** |
| `CHECK_EQ(a, b)` | 额外打印实际值和期望值 |
| `test_summary("名字")` | 在 `main` 结尾调用，返回 0（全过）或非 0（有失败） |

**返回非 0 是关键**——CTest 靠退出码判断测试有没有通过。

`test_handler.cpp` 目前还是打印式的，**没有加断言**，这是下一步要补的。

## 已知问题

- **`LoadEnv` 用相对路径读 `.env`**（`src/ai.cpp`）：`std::ifstream file(".env")` 是相对
  **当前工作目录**解析的，所以必须在放 `.env` 的那个目录里运行程序。
  更好的做法是逐级向上查找，或者用环境变量传入。
  同样的问题也存在于 `demo/ai_demo.cpp`
- **`demo/ai_demo.cpp` 重复实现了 `CallDeepSeek` 和 `load_env`**：和 `src/ai.cpp` 里几乎是
  逐行重复的。应该改成 `#include "ai.h"` 复用同一个实现，否则两边行为会漂移
- **调试输出没删干净**：`src/ai.cpp` 的 `Classify()` 里有一行
  `std::cout << "AI 原始响应: " << response`，生产环境会污染日志
- **`curl_global_init()` 没有调用**：libcurl 要求在主线程初始化一次。虽然现在会自动兜底，
  但在多线程环境下不推荐依赖这个行为
- **AI 没有单元测试**：`tests/` 里没有 `test_ai.cpp`。因为要发真实网络请求，
  它属于集成测试——想测的话应该先把 HTTP 调用抽成接口再注入假实现
- **fd 竞态**：`Server::run()` 收到可读事件后，**没有先把 fd 从 epoll 摘除**就丢进线程池，
  而读循环是在工作线程里做的（`src/server.cpp`）。`EPOLLET` 下如果请求分多个 TCP 段到达，
  同一个 fd 可能被投递两次任务，两个线程同时读同一个 fd——轻则数据错乱，重则 double close。
  修法是把读循环挪回 `Server::run()`，读完再 `EPOLL_CTL_DEL` 并入队
  （`single-file` 分支的 `http_server_et_loop.cpp` 就是这个写法）
- **所有工作线程共享同一个 `sqlite3*`**：SQLite 默认 serialized 模式，数据库操作实际是串行的，
  加线程数换不来吞吐。要真并发需要开 WAL（`PRAGMA journal_mode=WAL`）+ 每线程一条连接
- **AI 调用没有超时控制**：libcurl 没设 `CURLOPT_TIMEOUT`，网络卡住会一直等
- `content` 未做 JSON 转义，直接拼进响应；含引号的待办会破坏响应 JSON
- 没有请求体大小上限、没有 `keep-alive`；线程池队列无上限，连接没有背压

## 仓库说明

`.gitignore` 已覆盖：`build/`、`*.o`、`*.out`、`*.db`、`todo`、`*.log`、`.vscode/`、`.idea/`，
以及 **`.env` / `.env.*`**（但保留 `.env.example`）。

工作目录里会有 `todo.db`、`test.db` 等运行时产物——它们都被忽略了，不会误提交。

其他分支：

- **`single-file`** —— 六个单文件版本，含完整压测数据
- **`feature-*`** —— 开发中的功能，合回本分支后删除
