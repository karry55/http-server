#include "thread_pool.h"
#include <iostream>

int main() {
    ThreadPool pool(4);
    for (int i = 0; i < 10; i++) {
        pool.enqueue([i] {
            std::cout << "任务 " << i << std::endl;
        });
    }
    return 0;
}
