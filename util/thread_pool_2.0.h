//
// Created by 镜瞳 on 2023/4/6.
//

#ifndef THREAD_POOL_2_0_THREAD_POOL_H
#define THREAD_POOL_2_0_THREAD_POOL_H

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <semaphore>

namespace mirror {

    const int MAX_REQUESTS = 10000;

    template<typename taskType>
    class thread_pool{
    public:
        explicit thread_pool(unsigned int num = std::thread::hardware_concurrency());
        ~thread_pool();
        bool append(taskType* task);
    private:
        bool stop;
        int max_task_num;
        std::vector<std::thread> threads;
        std::queue<taskType*> task_queue;

        std::mutex mutex;
        std::counting_semaphore<MAX_REQUESTS> sem;
    };

    template<typename taskType>
    bool thread_pool<taskType>::append(taskType *task) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (task_queue.size() == max_task_num) {
                mutex.unlock();
                return false;
            }
            task_queue.push(task);
        }
        sem.release();
        return true;
    }

    template<typename taskType>
    thread_pool<taskType>::~thread_pool() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        for(auto& thread : threads) {
            //必须引用，不能复制
            thread.join();
        }
    }

    template<typename taskType>
    thread_pool<taskType>::thread_pool(unsigned int num) :stop(false), max_task_num(MAX_REQUESTS), sem(std::counting_semaphore<MAX_REQUESTS>(0)){
        for(int i = 0; i < num; ++i) {
            threads.emplace_back([this]{
                while(!stop || !task_queue.empty()) {
                    taskType* task;
                    {
                        sem.acquire();
                        std::unique_lock<std::mutex> lock(mutex);
                        if(stop && task_queue.empty()) return;
                        task = task_queue.front();
                        task_queue.pop();
                    }
                    task->process();
                }
            });
        }
    }
}

#endif //THREAD_POOL_2_0_THREAD_POOL_H
