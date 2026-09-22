#include "db.h"
#include "cache.h"
#include "handler.h"
#include "server.h"
#include "ai.h"

int main() {
    DB db("todo.db");
    Cache cache;
    AI ai;
    Handler handler(db, cache, ai);
    Server server(8080, handler);
    server.run();
    return 0;
}