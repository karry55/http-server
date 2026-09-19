# HTTP Todo Server

用 C++17 和 POSIX socket 从零实现的 HTTP 服务端，**没有使用任何 Web 框架**——
请求报文解析、路由分发、响应拼装全部手写。

同一个 Todo API 写了四个版本，每次只改一个维度（并发模型，再叠加缓存），方便横向对照性能与代码复杂度。

## 四个版本

| 文件 | 并发模型 | 数据层 | 链接选项 |
| --- | --- | --- | --- |
| `http_server.cpp` | 阻塞式 `accept`，一问一答 | SQLite | `-lsqlite3` |
| `http_server_epoll.cpp` | epoll 事件驱动（单线程） | SQLite | `-lsqlite3` |
| `http_server_thread_pool.cpp` | epoll 收连接 + 线程池处理业务（8 线程） | SQLite | `-lsqlite3 -pthread` |
| `http_server_redis.cpp` | epoll + 线程池（4 线程） | SQLite + Redis 缓存 | `-lsqlite3 -lhiredis -pthread` |

演进思路：

1. **先跑通** —— 最朴素的阻塞式循环，把 HTTP 解析、路由、SQLite 读写走通
2. **解决 IO 阻塞** —— 换成 epoll，单线程也能扛住大量空闲连接
3. **解决业务阻塞** —— 数据库读写扔进线程池，事件循环不再被业务拖住
4. **压掉热点读** —— `GET /todos` 的结果进 Redis，60 秒 TTL，写时主动失效

四个版本共用同一套接口与表结构，差别只在上面这两列。

## 接口

统一监听 **8080** 端口，响应均为 `application/json` + `Connection: close`。

| 方法 | 路径 | 说明 | 实现范围 |
| --- | --- | --- | --- |
| `GET` | `/` | 返回 `{"message": "home"}` | 全部版本 |
| `GET` | `/status` | 健康检查，返回 `{"status": "ok"}` | 全部版本 |
| `POST` | `/todo` | 新建待办，请求体即内容，返回 `{"status": "created"}` | 全部版本 |
| `GET` | `/todos` | 列出全部待办 | 全部版本（Redis 版走缓存） |
| `DELETE` | `/todo/<id>` | 删除指定待办，返回 `{"status": "deleted"}` | 仅阻塞版 |
| `POST` | `/echo` | 回显请求体，调试用 | 仅阻塞版 |

未匹配的路径返回 `404` + `{"error": "not found"}`；`DELETE /todo/` 后跟非数字返回
`400` + `{"error": "invalid id"}`。

### 缓存行为（`http_server_redis.cpp`）

- key：`todos_cache`
- **写路径**：`POST /todo` 插入成功后 `DEL todos_cache`
- **读路径**：`GET /todos` 先 `GET todos_cache`；未命中则查 SQLite，再 `SETEX todos_cache 60 <body>`
- 写缓存时用 `%b` 而非 `%s` 传参，保证 JSON 内容二进制安全
- Redis 连接是**按需惰性建立**的（`get_redis()`），不是启动时一次性连好
- 连接地址固定：`127.0.0.1:6379`

## 编译

依赖：

```bash
sudo apt install build-essential libsqlite3-dev libhiredis-dev
```

```bash
g++ -std=c++17 -O2 -o http_server             http_server.cpp             -lsqlite3
g++ -std=c++17 -O2 -o http_server_epoll       http_server_epoll.cpp       -lsqlite3
g++ -std=c++17 -O2 -o http_server_thread_pool http_server_thread_pool.cpp -lsqlite3 -pthread
g++ -std=c++17 -O2 -o http_server_redis       http_server_redis.cpp       -lsqlite3 -lhiredis -pthread
```

## 运行

```bash
./http_server          # 换成任意一个版本都一样
```

启动后监听 8080，并在**当前工作目录**创建 / 打开 `todo.db`。表结构由
`CREATE TABLE IF NOT EXISTS` 自动建好，不需要手动初始化。

Redis 版需要先起一个 Redis 实例：

```bash
redis-server --daemonize yes
./http_server_redis
```

### 试一下

```bash
curl -X POST -d 'buy milk'     http://127.0.0.1:8080/todo
curl -X POST -d 'write README' http://127.0.0.1:8080/todo

curl http://127.0.0.1:8080/todos
# [{"id": 1, "content": "buy milk"},{"id": 2, "content": "write README"}]

curl -X DELETE http://127.0.0.1:8080/todo/1   # 仅阻塞版支持
# {"status": "deleted"}

curl -X POST -d 'hello' http://127.0.0.1:8080/echo
# {"echo": "hello"}
```

### ⚠️ 必须在项目目录下运行

`todo.db` 是相对**当前工作目录**打开的，不是相对可执行文件的位置。在别处启动会在那个目录
新建一个空数据库，看起来就像"数据丢了"：

```bash
cd http-server && ./http_server   # ✅ todo.db 落在 http-server/
cd /tmp && /path/to/http_server   # ❌ 会在 /tmp 另建一个空 todo.db
```

## 性能

阻塞版在当前机器（WSL）上的压测记录约为 **QPS 32670**。

## 已知取舍

这是练习代码，为保持每个版本尽量短，有几处刻意没有做：

- `content` 未做 JSON 转义，直接拼进响应；含引号的待办会破坏 JSON
- 缓存是"整表一个 key"，任何一次写入都会让整个列表失效
- 没有请求体大小上限、没有超时控制、没有 `keep-alive`

## 仓库说明

编译产物（`http_server`、`http_server_epoll`、`http_server_redis`、
`http_server_thread_pool`，都没有扩展名）、数据库（`*.db`）和编辑器配置已由
`.gitignore` 排除，不会入库。
