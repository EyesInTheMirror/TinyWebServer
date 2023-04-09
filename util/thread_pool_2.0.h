//
// Created by mirror on 23-4-5.
//

#ifndef WEBSERVER_CPP11_THREAD_POOL_2_0_H
#define WEBSERVER_CPP11_THREAD_POOL_2_0_H

#include <thread>

class thread_pool {
public:
    explicit thread_pool(int thread_num = std::thread::hardware_concurrency());
};


#endif //WEBSERVER_CPP11_THREAD_POOL_2_0_H
