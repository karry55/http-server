# HTTP Todo Server（模块化版）

用 **C++17** 和 POSIX socket 从零实现的 HTTP Todo 服务端，**不使用任何 Web 框架**——
请求报文解析、路由分发、响应拼装全部手写。

功能拆成**六个可独立构造、可单独测试的模块**，用 CMake 构建，接入 Redis 缓存与
DeepSeek AI 能力，并配了一个纯前端页面。

> 六个递进式的单文件实现（从阻塞 `accept` 一路演进到 epoll + 线程池 + Redis 缓存，
> 含完整压测数据与瓶颈分析）在 **`single-file` 分支**（GitHub 上是同名分支）。

## 技术栈

| 层次 | 用了什么 |
| --- | --- |
| 网络 | POSIX socket + **epoll**（边沿触发 `EPOLLET`） |
| 并发 | 手写线程池（`pthread` + 条件变量），固定 4 线程 |
| 存储 | SQLite3（参数绑定，防 SQL 注入） |
| 缓存 | Redis（hiredis，`thread_local` 连接） |
| AI | libcurl + nlohmann/json 调用 DeepSeek |
| 构建 | CMake + CTest |
| 前端 | 单文件 HTML（`frontend/index.html`） |

## 目录结构

```
.
├── CMakeLists.txt            构建配置（开启 -Wall -Wextra -Wshadow）
├── main.cpp                  组装六个对象，27 行
├── .env                      真实密钥，**不入库**（见「配置 API Key」）
├── .env.example              模板，只有键名，可以入库
├── .vscode/                  VS Code 调试与任务配置（被 gitignore 忽略）
│   ├── launch.json             F5 调试 / Ctrl+F5 运行
│   ├── tasks.json              cmake 构建任务
│   └── c_cpp_properties.json   IntelliSense 的 includePath
├── include/                  接口声明
│   ├── server.h                socket + epoll + 线程池调度
│   ├── handler.h               路由分发，返回 (status, body)
│   ├── db.h                    SQLite 封装
│   ├── cache.h                 Redis 封装
│   ├── ai.h                    DeepSeek 调用封装
│   ├── thread_pool.h           通用线程池
│   └── check.h                 测试断言宏（CHECK / CHECK_EQ / test_summary）
├── src/                      对应实现
│   ├── server.cpp             166 行
│   ├── handler.cpp            166 行
│   ├── ai.cpp                 132 行
│   ├── db.cpp                  80 行
│   ├── thread_pool.cpp         40 行
│   └── cache.cpp               31 行
├── tests/                    单元测试（已接入 CTest）
│   ├── test_db.cpp            12 行，10 条断言
│   ├── test_cache.cpp         15 行，打印式（TODO：补断言）
│   ├── test_handler.cpp       78 行，20 条断言
│   └── test_thread_pool.cpp   11 行，打印式（TODO：补断言）
├── frontend/
│   └── index.html             纯前端页面（增删改查 + 三个 AI 按钮）
└── demo/
    └── ai_demo.cpp            独立的 AI 调用示例（不依赖服务器）
```

## 架构

`main.cpp` 只负责组装，不含任何业务逻辑：

```cpp
DB db(db_path());                 // 数据库（路径由可执行文件位置推导）
Cache cache;                      // 缓存
AI ai;                            // AI
Handler handler(db, cache, ai);   // 依赖注入
Server server(8080, handler);
server.run();
```

| 模块 | 文件 | 职责 |
| --- | --- | --- |
| `Server` | `include/server.h` · `src/server.cpp` | socket、epoll、把连接派发给线程池 |
| `Handler` | `include/handler.h` · `src/handler.cpp` | 路由分发，返回 `(status, body)` |
| `DB` | `include/db.h` · `src/db.cpp` | SQLite 封装：`addTodo` / `getTodos` / `getTodo` / `updateTodo` / `deleteTodo` |
| `Cache` | `include/cache.h` · `src/cache.cpp` | Redis 封装：`get` / `set` / `del` |
| `AI` | `include/ai.h` · `src/ai.cpp` | 调 DeepSeek API：分类 / 优先级 / 总结 |
| `ThreadPool` | `include/thread_pool.h` · `src/thread_pool.cpp` | 通用线程池，固定 4 线程 |

**分层带来的实际收益**：`Handler` 不碰 socket，所以测试能直接调 `handle()`
（`tests/test_handler.cpp` 就是不经过网络直接调用）；`DB` 和 `Cache` 也各自能单独构造，
不依赖服务器先跑起来。

### 一次请求的完整数据流

```
浏览器 → GET /todos
   ↓
Server::run()         epoll_wait 感知可读事件
   ↓
pool_.enqueue(...)    连接 fd 丢进线程池（工作线程处理）
   ↓
Server::handle_client()   读数据 → 解析出 method="GET", path="/todos"
   ↓
Handler::handle()     先查缓存 "todos_cache"
   ├─ 命中 → 直接返回缓存的 JSON（不进数据库、不重新拼字符串）
   └─ 未命中 → DB::getTodos() → 拼 JSON → 写回缓存(TTL 60s) → 返回
   ↓
拼装响应（状态行 + 响应头 + body），write() 发回，close()
```

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
| `POST` | `/todo/classify` | **AI 分类**，返回 `{"category": "..."}` |
| `POST` | `/todo/prioritize` | **AI 优先级**，返回 `{"priority": "高/中/低"}` |
| `POST` | `/todo/summarize` | **AI 总结**，返回 `{"summary": "..."}` |
| `OPTIONS` | 任意路径 | CORS 预检，返回 `204 No Content` |

### 状态码

| 场景 | 返回 |
| --- | --- |
| 路径参数非数字 / 超范围 / 小于 1 | `400` + `{"error": "invalid id"}` |
| 未匹配的路径 | `404` + `{"error": "not found"}` |
| 单条不存在 | `404` + `{"error": "not found"}` |
| 数据库操作失败 | `500` + `{"error": "..."}` |
| 请求体超过 1 MB（**不可靠，见下方说明**） | `413` + `{"error": "request too large"}` |

## 安全与输入校验

这一层是项目的重点，每一条都对应一个真实修过的坑：

| 措施 | 位置 | 防的是什么 |
| --- | --- | --- |
| `parse_id()` 做范围校验 | `src/handler.cpp` | **超大 id 会让整个进程崩溃**。`std::stoi` 在数字超出 `int` 范围时抛 `std::out_of_range`，异常逃出线程函数会触发 `std::terminate()` |
| 线程池任务外包 `try/catch` | `src/thread_pool.cpp` | 兜底：任何任务抛异常都不能杀死进程 |
| `json_escape()` | `src/handler.cpp` | 待办内容含 `"` 会破坏响应 JSON，**整个列表在前端都刷不出来** |
| 请求体 1 MB 上限 | `src/server.cpp` | **实现有缺陷，不能可靠拦截，见下方「已发现的严重缺陷」** |
| AI 调用超时（连接 5s / 总 15s） | `src/ai.cpp` | 外部服务卡死会占满 4 个工作线程，**整个服务失去响应** |
| `safe_origin()` 校验 Origin | `src/server.cpp` | CRLF 响应头注入 |
| SQL 参数绑定（`?` + `bind`） | `src/db.cpp` | SQL 注入 |
| `content ? content : ""` | `src/db.cpp` | `sqlite3_column_text` 遇 NULL 返回空指针，直接构造 `std::string` 会崩 |

### 一个真实案例：超大 id 导致服务崩溃

```bash
curl http://127.0.0.1:8080/todo/99999999999999999999
# 修复前：进程直接死掉（terminate called after throwing 'std::out_of_range'）
# 修复后：{"error": "invalid id"}
```

原来的校验只检查了「**格式**」（每个字符都是数字），漏了「**范围**」（数字放不进 `int`）。
`99999999999999999999` 格式完全合法，但 `int` 最大约 21 亿。

修法分两层，各自解决不同问题：

1. **`handler` 层**：`parse_id()` 返回 `false` → 给用户正确的 `400` 响应
2. **`thread_pool` 层**：`try/catch` 兜底 → 保证进程不死

已有回归测试守着（`test_handler.cpp` 里的边界测试：`2147483647` 应返回 `404`，
`2147483648` 应返回 `400`）。

## CORS

页面是 `file://` 打开的（Origin 为 `null`），后端在 `127.0.0.1:8080`，属于跨域。
后端统一返回这些响应头（`src/server.cpp`）：

```
Access-Control-Allow-Origin: <回显请求的 Origin，无 Origin 时用 *>
Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS
Access-Control-Allow-Headers: Content-Type
Access-Control-Max-Age: 86400
Vary: Origin
```

`OPTIONS` 预检在 `Handler::handle()` 里直接返回 `204 No Content`（`204` 不能带 body）。

**注意**：现在是**回显任意 Origin**，方便本地开发。如果要部署到公网，必须改成白名单。

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

`SETEX` 一条命令同时设置值和过期时间，比 `SET` + `EXPIRE` 更安全。

> ⚠️ **测试与正式服务共用 Redis**。`test_handler` 曾经把测试数据写进 `todos_cache`，
> 导致正式服务返回假数据。现在测试已改用 `:memory:` 数据库来避免这个问题；
> 如果本地遇到"数据看起来不对"，可以先清缓存验证：
> `redis-cli del todos_cache`

## AI 集成

`AI` 类提供三个能力，都通过同一个 `CallDeepSeek()` 发请求：

| 方法 | 接口 | 提示词要点 | 兜底值 |
| --- | --- | --- | --- |
| `Classify()` | `POST /todo/classify` | 限定「购物/工作/学习/其他」四选一 | `"其他"` |
| `Prioritize()` | `POST /todo/prioritize` | 给了明确判断标准 + 「只回答一个汉字」 | `"中"` |
| `Summarize()` | `POST /todo/summarize` | 「用一句话总结」 | `"总结失败"` |

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

`Prioritize()` 的提示词最用心，给了明确的判断标准：

```
请判断下面这条待办的优先级，按「紧急程度 + 重要程度」评估。
判断标准：
高 = 今天必须完成，或有明确截止时间，或不做会有严重后果
中 = 这周内应该完成，重要但不紧急
低 = 有空再做即可，没有时间压力
只回答一个汉字：高、中 或 低，不要输出任何其他文字。
待办：<你的输入>
```

**模型返回的内容会先 `trim()` 去掉首尾空白**（模型常返回 `"工作\n"`），再 `json_escape()`
转义后拼进 JSON。

**解析失败时走兜底值**，所以 AI 挂掉不会让服务崩掉，只是结果不准。

### 超时设置

```cpp
curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);   // 建立连接最多 5 秒
curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);         // 整个请求最多 15 秒
```

**为什么必须有**：线程池只有 4 个线程。没有超时的话，一旦 DeepSeek 卡住，
4 个线程很快被占满，**连 `/status` 都打不开**。

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

程序会自动定位 `.env`：**先看当前工作目录，找不到就回到可执行文件所在目录的上一级**
（即项目根目录）。所以不需要固定在某个目录启动。

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

编译选项里已开启 **`-Wall -Wextra -Wshadow`**（见 `CMakeLists.txt`）。
目前全部源文件在这组警告下**零警告**。先不加 `-Werror`，否则任何警告都会导致编译失败。

### 运行

```bash
# 如果 Redis 不是开机自启的服务，手动起来
redis-server --daemonize yes

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

# AI 三个能力
curl -X POST -d '明天要交周报' http://127.0.0.1:8080/todo/classify     # {"category":"工作"}
curl -X POST -d '明天要交周报' http://127.0.0.1:8080/todo/prioritize   # {"priority":"高"}
curl -X POST -d '买牛奶；买资料' http://127.0.0.1:8080/todo/summarize  # {"summary":"..."}

# 防御机制验证
curl http://127.0.0.1:8080/todo/99999999999999999999   # {"error":"invalid id"}（服务不死）
curl http://127.0.0.1:8080/status                       # 确认服务还活着
```

### 数据与配置文件的定位

`todo.db` 和 `.env` **不依赖当前工作目录**：程序用 `/proc/self/exe` 拿到自己的真实路径，
再取上一级定位项目根目录，所以从哪个目录启动都读同一份文件。

```bash
./build/http_server              # ✅ 项目根目录
cd build && ./http_server        # ✅ 也可以，读的还是根目录那份
```

### 在 VS Code 里运行（推荐）

项目已配好 `.vscode/`：

- **`F5`** —— 启动调试（会先自动执行 `cmake-build` 任务，可打断点）
- **`Ctrl+F5`** —— 直接运行，不调试
- **`Ctrl+Shift+B`** —— 只编译
- **停止**：`Shift+F5` 或点调试工具栏的红色方块 ⏹

`launch.json` 里设了 `"cwd": "${workspaceFolder}"`，保证工作目录是项目根目录。

> ⚠️ **必须用 WSL 方式打开项目**（左下角显示 `WSL: Ubuntu`）。
> 源码用了 `<sys/epoll.h>` / `<unistd.h>`，只能在 Linux 下编译运行。

## 测试

四个模块的单元测试**已经接进 CMake / CTest**，用 `include/check.h` 里的断言宏判断对错，
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
1/4 Test #1: test_db ..........................   Passed    0.01 sec
    Start 2: test_thread_pool
2/4 Test #2: test_thread_pool .................   Passed    0.00 sec
    Start 3: test_cache
3/4 Test #3: test_cache .......................   Passed    0.00 sec
    Start 4: test_handler
4/4 Test #4: test_handler .....................   Passed    0.01 sec

100% tests passed, 0 tests failed out of 4
```

`--output-on-failure` 表示只有失败的用例才把输出贴出来。

### 看每个测试跑了多少条断言

CTest 默认只显示通过/失败，**不显示测试自己打印的内容**。加 `-V` 才能看到：

```bash
ctest --test-dir build -V
# 4: test_handler: 20 通过, 0 失败
```

这个数字很重要：如果显示 `0 通过, 0 失败`，说明断言一条都没执行，属于假通过。

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

| 测试 | 覆盖内容 | 断言数 | 需要 Redis |
| --- | --- | --- | --- |
| `test_db` | 空库、增、查、查不到（返回 `-1`）、改、删 | **10 条** | ❌（用 `:memory:`） |
| `test_cache` | `set` / `get` / `del` | 打印式 ⚠️ | ✅ |
| `test_handler` | 直接调 `Handler::handle()`，覆盖 11 组场景 | **20 条** | ✅ |
| `test_thread_pool` | 投 10 个任务 | 打印式 ⚠️ | ❌ |

`test_handler` 覆盖的场景包括：`GET /`、`GET /status`、`POST /todo`、`GET /todos`
（检查返回是不是合法数组）、`404` 未匹配路径、非法 id（`abc` / `0` / 超大数字）、
**边界测试**（`2147483647` → `404`，`2147483648` → `400`）、
`PUT` / `DELETE` 的非法 id、`OPTIONS` 预检。

**注意**：`test_db` 和 `test_handler` 都用 `DB db(":memory:")`——内存数据库，
每次都是全新的，测试之间不会互相污染，也不会在磁盘上留垃圾文件。

> ⚠️ `test_cache` 和 `test_thread_pool` **目前还是打印式**，只 `cout` 然后 `return 0`，
> 所以它们**永远显示"通过"**——Redis 挂掉、线程分配错了都发现不了。
> 这是下一步要补的（宏已经现成，把 `cout` 换成 `CHECK` 即可）。

### 断言宏

`include/check.h` 提供了三个工具：

| 宏 / 函数 | 作用 |
| --- | --- |
| `CHECK(cond)` | 条件为假时记一次失败，打印出错的文件与行号，**继续往下跑** |
| `CHECK_EQ(a, b)` | 额外打印实际值和期望值 |
| `test_summary("名字")` | 在 `main` 结尾调用，返回 0（全过）或非 0（有失败） |

**返回非 0 是关键**——CTest 靠退出码判断测试有没有通过。

### 验证测试是否真的有效（变异测试）

故意把代码改坏，看测试会不会失败：

```cpp
// src/handler.cpp 的 parse_id() 里，把范围检查改掉
if (v < 1 || v > 2147483647L) return false;   // 改前（正确）
if (v < 1) return false;                       // 改后（故意去掉上界检查）
```

重新编译并跑测试，**应该看到失败**（比如 `18 通过, 2 失败`）。
如果还是全过，说明测试写错了。验证完记得改回来。

## 已完成的安全加固

以下问题曾经存在，现在已经修复（详细说明见「安全与输入校验」）：

- ✅ 超大 id 导致**服务进程崩溃**（`std::stoi` 抛异常 + 线程池无兜底）
- ✅ 待办内容含引号导致**响应 JSON 非法**，前端整个列表刷不出来
- ✅ AI 调用**无超时**，外部服务卡住会拖垮整个线程池
- ✅ `todo.db` / `.env` 用相对路径，**从不同目录启动会读错文件**
- ✅ 前端跨域失败（后端缺 CORS 响应头 + `OPTIONS` 预检返回 404）
- ✅ AI 返回值含结尾换行（`"工作\n"`）污染 JSON
- ✅ `test_handler` 是打印式假测试，改为 20 条真实断言
- ⚠️ 请求体大小上限**已加但没有真正生效**——见「已知问题」的第一条

## 已知问题与改进方向

按优先级排列，前几项是建议下一步做的：

### 高优先级

- **🔴 `Content-Length` 未校验，请求体不完整时会被当成完整请求处理（数据损坏 + 上限失效）**

  这是目前发现的最严重问题。`Server::handle_client()` **完全不解析 `Content-Length`**，
  它的逻辑是「循环 `read` 到 `EAGAIN`（暂时没数据了）就认为请求收完了」。
  但 `EAGAIN` 只代表"此刻缓冲区空了"，**不代表客户端已经发完**。

  实测证据（声明 body 为 100000 字节，实际只发 1000 字节）：

  ```
  只发了 1000/100000 字节，服务器立刻响应了：HTTP/1.1 200 OK
  数据库最新记录: (id=23, 长度=1000)   ← 半包内容被当成完整待办存进去了
  ```

  **两个后果**：

  1. **数据静默损坏**：客户端发得慢、或中途断开，剩余内容就被丢掉，
     而**截断后的内容会被当成一条完整待办写入数据库**。用户不会收到任何错误提示。
  2. **1 MB 上限形同虚设**：上限检查（`request.size() > MAX_REQUEST_SIZE`）只在
     **单次累积超过 1 MB** 时才触发。而 TCP 是流式的，服务端往往在累积到
     1 MB 之前就遇到 `EAGAIN` 跳出循环，于是检查永远不会命中。实测：

     | 发送方式（总大小都是 2 MB） | 实际结果 |
     | --- | --- |
     | 一次性发完 | `200 OK` ❌ 应为 413 |
     | 每 256 KB 停顿 10 ms | 只发出 0.5 MB 就被服务端重置，返回 `200 OK` ❌ |

     （对照实验：把上限临时改成 10 字节时 413 能正常触发，证明**代码逻辑本身没错，
     是触发时机不可靠**。）

  **正确修法**：

  1. 从请求头里解析出 `Content-Length`；缺失或非法 → 返回 `400`
  2. `Content-Length > MAX_REQUEST_SIZE` → **在读 body 之前**就返回 `413`
     （而不是等读超了才判断）
  3. `\r\n\r\n` 之后累积到 `Content-Length` 指定的字节数，才认为请求完整；
     不足就继续 `read`（配合 `epoll` 等待下一次可读事件，而不是直接 `break`）
  4. 注意 `Content-Length` 是**字节数**，不是字符数

  目前项目里的**其他防御机制（`parse_id`、`json_escape` 等）都有效**，
  只有这一条没有达到预期效果。

- **`test_cache` / `test_thread_pool` 是打印式测试**：永远返回 0，不起保护作用。
  改法：用 `include/check.h` 的 `CHECK` 替换 `cout`，结尾 `return test_summary(...)`；
  `test_thread_pool` 还需要等任务跑完再断言（现在没等就返回，输出顺序随机）
- **fd 竞态**：`Server::run()` 收到可读事件后**没有先把 fd 从 epoll 摘除**就丢进线程池，
  读循环在工作线程里做。`EPOLLET` 下如果请求分多个 TCP 段到达，同一个 fd 可能被投递两次，
  两个线程同时读同一个 fd——轻则数据错乱，重则 double close。
  修法：把读循环挪回 `Server::run()`，读完再 `EPOLL_CTL_DEL` 并入队
  （`single-file` 分支的 `http_server_et_loop.cpp` 就是这个写法）

### 中优先级

- **所有工作线程共享同一个 `sqlite3*`**：SQLite 默认 serialized 模式，数据库操作实际是
  串行的，加线程数换不来吞吐。要真并发需要开 WAL（`PRAGMA journal_mode=WAL`）
  + 每线程一条连接
- **线程池队列无上限**，也没有背压：任务可以无限堆积
- **没有 `keep-alive`**：每个请求都 `Connection: close`，连接建立开销大
- **没有优雅关闭**：收到 `SIGINT` 时直接退出，在途请求会被中断
- **`curl_global_init()` 没有调用**：libcurl 要求在主线程初始化一次。虽然现在会自动兜底，
  但在多线程环境下不推荐依赖这个行为
- **AI 没有单元测试**：要发真实网络请求，属于集成测试。想测的话应该先把 HTTP 调用
  抽成接口再注入假实现（现在 `Handler` 拿到的是具体的 `AI&`，不好替换）

### 低优先级

- **`demo/ai_demo.cpp` 重复实现了 `CallDeepSeek` 和 `load_env`**：和 `src/ai.cpp` 里几乎
  是逐行重复的。应该改成 `#include "ai.h"` 复用同一个实现，否则两边行为会漂移
- **调试输出没删干净**：`src/ai.cpp` 的 `Classify()` 里有一行
  `std::cout << "AI 原始响应: " << response`，生产环境会污染日志
- **CORS 回显任意 Origin**：本地开发够用，部署到公网必须改成白名单

## 仓库说明

`.gitignore` 已覆盖：`build/`、`*.o`、`*.out`、`*.db`、`todo`、`*.log`、`.vscode/`、`.idea/`，
以及 **`.env` / `.env.*`**（但保留 `.env.example`）。

工作目录里会有 `todo.db` 等运行时产物——它们都被忽略了，不会误提交。

> 注意 `.vscode/` 被忽略，所以 `launch.json` / `tasks.json` 不入库。
> 如果想让别人也能一键调试，可以改成只忽略 `.vscode/settings.json`。

其他分支：

- **`single-file`** —— 六个单文件版本，含完整压测数据
- **`feature-*`** —— 开发中的功能，合回本分支后删除
