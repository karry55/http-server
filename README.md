# HTTP Todo Server

用 C++17 和 POSIX socket 从零实现的 HTTP 服务端，**没有使用任何 Web 框架**——
请求报文解析、路由分发、响应拼装全部手写。

同一个 Todo API 写了四个版本，每次只改一个维度（并发模型，再到数据层缓存），
每个版本都做了压测，用数据说明瓶颈迁移到了哪里。

## 四个版本

| 文件 | 并发模型 | 数据层 | 链接选项 |
| --- | --- | --- | --- |
| `http_server.cpp` | 阻塞式 `accept`，一问一答 | SQLite | `-lsqlite3` |
| `http_server_epoll.cpp` | epoll 事件驱动（单线程） | SQLite | `-lsqlite3` |
| `http_server_thread_pool.cpp` | epoll 收连接 + 线程池处理业务（4 线程） | SQLite | `-lsqlite3 -pthread` |
| `http_server_redis.cpp` | epoll + 线程池（4 线程） | SQLite + Redis 缓存 | `-lsqlite3 -lhiredis -pthread` |

四个版本共用同一套接口与表结构，差别只在上面这两列。压测结果见 [性能](#性能)。

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
- Redis 连接是**按需惰性建立**的（`get_redis()`），且用 `thread_local` 保证每个工作线程一条独立连接
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

### 实测数据

本机 WSL，`wrk` 压测。V4 是把 V3 的线程数从 4 改成 8 后重跑的结果；V5 与 V3 线程数完全相同，
唯一差异是多了一层 Redis 缓存。

| 版本 | 文件 | 线程数 | QPS | 延迟 Avg | 延迟 Stdev | 延迟 Max | ± Stdev | 错误数 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| V1 阻塞版 | `http_server.cpp` | 1 | 0 | —— | —— | —— | —— | —— |
| V2 epoll 版 | `http_server_epoll.cpp` | 1 | 1094 | 15.07 ms | 50.82 ms | 1.67 s | 98.78% | 3 timeout |
| V3 线程池版 | `http_server_thread_pool.cpp` | 4 | 1889 | 50.27 ms | 14.49 ms | 260.77 ms | 78.20% | 0 |
| V4 线程池版 | 同上，线程数改为 8 | 8 | 1887 | 48.66 ms | 18.98 ms | 311.66 ms | 83.22% | 0 |
| V5 Redis 版 | `http_server_redis.cpp` | 4 | **32670** | **2.67 ms** | 6.06 ms | 213.88 ms | 99.73% | 0 |

### 逐版本解读

**V1 阻塞版 —— QPS 为 0：被 `accept` 和 `read` 的双重阻塞锁死。**

这是唯一一个 QPS 为 0 的版本，原因就在它的 IO 模型上：

- `accept(server_fd, ...)`（`http_server.cpp:69`）在阻塞 socket 上等待新连接
- `read(client_fd, buffer, ...)`（`:77`）在阻塞 socket 上等待请求数据

两次调用都是阻塞的，意味着服务器同一时刻只能停在其中一个上面：

- 停在 `accept` 时，它在等新连接，已有连接上的请求没人管
- 停在 `read` 时，它在等当前这条连接把请求发过来，**新连接即使已经完成三次握手、
  躺在 accept 队列里，也拿不到处理**

于是整个服务被串行化成"一次只伺候一个连接的一小段过程"。`listen(server_fd, 10)` 只给了
10 个 backlog 位置，并发一上来队列就溢出，后续连接连排队都排不上。

结果是并发压测下请求全部堵在排队和超时上，压测窗口内没有一个请求走完完整往返，QPS 记 0。

要让这一格出现非零数字，就必须改 IO 模型——这正是 V2 要做的事。

**V2 epoll 版 —— 连接等得起了，长尾很痛。**
换成 epoll + 非阻塞后第一次跑出数据：1094 QPS。但延迟分布很难看——Avg 只有 15.07 ms，
Max 却到了 1.67 秒，还有 3 个超时。

原因是 epoll 事件循环里**直接做了 SQLite 读写**：一次数据库操作期间，其他所有连接都被阻塞在原地。
绝大多数请求很快（所以 Avg 低），但恰好撞上数据库操作的请求会被拖到秒级。

**V3 线程池版（4 线程）—— 主要收益是消除长尾，不是提高吞吐。**
把"解析 + SQLite + 写响应"整段挪进线程池后：

- QPS 1094 → 1889（+73%）
- Max 延迟 1.67 s → 260.77 ms
- 超时 3 → 0
- Stdev 50.82 ms → 14.49 ms

但 Avg 从 15.07 ms 涨到 50.27 ms。这不是退化，而是模型变了：V2 是"大多数人很快、少数人极慢"，
V3 是"大家一起排队"。尾巴收掉了、总吞吐上去了，代价是每个请求要多等一会儿队列。

**V4 线程池版（8 线程）—— 吞吐纹丝不动。**
只把线程数从 4 调到 8，QPS 1889 → 1887，差 0.1%，在噪声范围内。

**这是整份数据里信息量最大的一行**：它说明瓶颈不在工作线程的数量上。

原因在数据层——四个版本共用同一个全局 `sqlite3*` 连接，而 SQLite 默认是 serialized 模式，
每次操作都要抢同一把互斥锁。线程从 4 加到 8，只是让更多线程排队等同一把锁，
真正执行 SQL 的部分仍然是串行的。

结论：线程池版想继续提速，该动的不是线程数，而是数据层——开 WAL
（`PRAGMA journal_mode=WAL`）让读写不再互斥，或者每线程一个连接 / 上连接池。
这个版本已经用数据证明"加线程"这条路的天花板到了。

**V5 Redis 版（4 线程）—— 绕过瓶颈，而不是缩小瓶颈。**
线程数和 V3 完全一样（都是 4），唯一改动是在数据层前面加一层 Redis 缓存：
`GET /todos` 先查缓存（60 秒 TTL），`POST /todo` 时删缓存。

- QPS 1889 → 32670，约 **17 倍**
- Avg 延迟 50.27 ms → 2.67 ms
- ± Stdev 从 78.20% 提升到 99.73%，延迟分布变得非常集中

因为两次跑的线程数、事件循环、HTTP 解析代码都没变，这 17 倍**只能来自缓存命中**——
热点读请求根本没走到 SQLite。反过来看，这也再次印证了 V3/V4 的瓶颈确实卡在 SQLite 上。

### 为什么 V5 能快 17 倍

V5 与 V3 的差异只有缓存这一层，所以这 17 倍全部来自"热路径上不再碰 SQLite"。拆开看是三块：

**一、绕开了 SQLite 的串行化锁（最主要的一块）**

V3/V4 共用同一个全局 `sqlite3*`，SQLite 默认 serialized 模式，每次操作都要抢同一把互斥锁。
V3 → V4 把线程数从 4 加到 8、吞吐却纹丝不动，已经用数据证明了这把锁就是天花板。
V5 的读路径走 Redis，压根不进 SQLite，这把锁自然也就不再是瓶颈。

**二、省掉了 SQL 解析 + 行扫描 + JSON 拼接这一整段 CPU 工作**

缓存里存的是**整份序列化好的响应体**。命中时一次 `GET todos_cache` 就把 body 直接拿到了，
既不需要 `sqlite3_prepare_v2` 解析语句，也不需要逐行 `sqlite3_step` 迭代，
更不需要在循环里反复做字符串拼接。

V5 的 Avg 2.67 ms 里，到 Redis 的本地往返只占一小部分，剩下省下的主要是这段
"取数据 + 拼 JSON" 的活儿。

**三、`thread_local` 消除了客户端侧的连接争用**

hiredis 的 `redisContext` 不是线程安全的，多个线程共用一条连接必须自己加锁，否则会直接数据错乱。
用 `thread_local` 让每条连接只属于一个线程，客户端侧就不存在任何互斥操作了。

需要说明的是：这**不等于**"Redis 内部并发无锁"。Redis 服务端执行命令本身是单线程串行的，
多个客户端连接发来的命令依然一条接一条执行。`thread_local` 解决的是**客户端**的并发安全与争用，
不是让 Redis 变成并行执行。

### 两个容易搞混的点

**「Redis 在内存、SQLite 在磁盘，所以快 1000 倍」——套在这个项目上不成立。**

- SQLite 有页缓存（默认约 2 MB），`todos` 表很小，**首次读之后整张表就在内存里**，
  热读路径并不落盘。所以 V5 的优势不是"内存 vs 磁盘"，而是"不进 SQLite"——
  省掉的是锁、SQL 解析、行扫描和 JSON 拼接这些开销
- 另外，进程内读一条缓存页上的数据本身是**微秒级**的，不是毫秒级
- 真正会体现磁盘差距的是**写路径**：SQLite 默认 rollback journal 模式，每次写事务都要 fsync
- 但 V5 的写路径（`POST /todo`）和 V3 完全一样，甚至还要多一次 `DEL`。
  也就是说，**如果压测打的全是写请求，V5 不会更快，反而会略慢**

**缓存省掉的是"读"，不是"写"。**

整表一个 key 的设计意味着：任何一次 `POST /todo` 都会让列表缓存失效，
下一个 `GET /todos` 又要回落到 SQLite 重新扫描并重建缓存。
读多写少的场景下这个策略很划算；写一多，缓存命中率就会崩掉。

### 四步迭代分别在解决什么

| 迭代 | 解决的问题 | 手段 | 结果 |
| --- | --- | --- | --- |
| V1 → V2 | `accept` / `read` 阻塞导致连接无法并发 | epoll + 非阻塞 socket | 从 0 QPS 到 1094 QPS，但长尾 1.67 s |
| V2 → V3 | 单线程被数据库操作拖住 | 业务处理挪进线程池 | 吞吐 +73%，尾延迟收掉，超时归零 |
| V3 → V4 | （证伪）设想加线程能提速 | 4 → 8 线程 | 吞吐没动，暴露真正的瓶颈在数据层 |
| V4 → V5 | 数据层的锁竞争 | 加 Redis 缓存绕开热点读 | 17 倍 |

### 测试口径与局限

- 工具 `wrk`，环境 WSL
- **本轮没有记录具体的接口配比**（打了哪些路径、各占多少比例），所以上面这组数字
  适合看横向趋势，但无法独立复现
- 要让这组对比站得住，建议固定一个明确的负载（例如 80% `GET /todos` + 20% `POST /todo`），
  同时记录并发连接数、压测时长、数据库行数，再重跑一遍

## 已知取舍

这是练习代码，为保持每个版本尽量短，有几处刻意没有做：

- `content` 未做 JSON 转义，直接拼进响应；含引号的待办会破坏 JSON
- 缓存是"整表一个 key"，任何一次写入都会让整个列表失效
- 没有请求体大小上限、没有超时控制、没有 `keep-alive`
- epoll 系列（V2/V3/V5）注册了 `EPOLLET`，但每次事件只 `read` 一次、未循环读到 `EAGAIN`。
  请求体超过 4096 字节时会被截断；正确做法是维护 per-connection 读缓冲直到凑齐完整报文

## 仓库说明

编译产物（`http_server`、`http_server_epoll`、`http_server_redis`、
`http_server_thread_pool`，都没有扩展名）、数据库（`*.db`）和编辑器配置已由
`.gitignore` 排除，不会入库。
