#include "db.h"
#include "cache.h"
#include "handler.h"
#include "server.h"
#include "ai.h"
#include <string>
#include <unistd.h>     // readlink
#include <limits.h>     // PATH_MAX

// 算出数据库的完整路径：可执行文件在 <项目根>/build/http_server，
// 所以往上一级就是项目根目录，todo.db 固定在那里。
// 这样无论从哪个文件夹启动，读到的都是同一个数据库，不会再多出一份。
static std::string db_path() {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "todo.db";          // 取不到就退回原行为
    buf[n] = '\0';
    std::string path(buf);
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return "todo.db";
    return path.substr(0, slash + 1) + "../todo.db";
}

int main() {
    DB db(db_path());
    Cache cache;
    AI ai;
    Handler handler(db, cache, ai);
    Server server(8080, handler);
    server.run();
    return 0;
}
