#include "db.h"
#include "cache.h"
#include "handler.h"
#include "server.h"

int main() {
    DB db("todo.db");
    Cache cache;
    Handler handler(db, cache);
    Server server(8080, handler);
    server.run();
    return 0;
}
