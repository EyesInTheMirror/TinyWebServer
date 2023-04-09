#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <list>
#include "locker.h"
#include <exception>
#include <cstdio>

template<typename T>
class thread_pool {
public:
    explicit thread_pool(int thread_num = 12, int max_requests = 10000);
    ~thread_pool() {
        is_stopped = true;//通知子线程结束循环
        for(int i = 0; i < m_thread_num; ++i) {
            pthread_join(m_threads[i], nullptr);
        }
        delete[] m_threads;//手动释放堆空间
    }
    bool append(T * request);

private:
    static void* worker(void* arg);
    void run();

    int m_thread_num;
    pthread_t * m_threads;

    int m_max_requests; //请求队列的最大长度
    std::list<T*> m_workqueue; //请求队列

    mutex queue_locker;
    sem queue_stat;

    bool is_stopped;
};

template<typename T>
thread_pool<T>::thread_pool(int thread_num, int max_requests): m_thread_num(thread_num), m_max_requests(max_requests), is_stopped(false) {
    if(thread_num <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_num];
//    if(m_threads == nullptr) {
//        throw std::exception();
//    }
// 没必要，new失败会抛出bad_alloc异常，malloc才是返回nullptr

    for(int i = 0; i < thread_num; ++i) {
        printf("creating thread %d\n", i);
        if(pthread_create(&m_threads[i], NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }
        //此处选择不分离，在析构时等待线程回收，避免访问无效的成员变量
        //pthread_detach(m_threads[i]);
    }
}

template<typename T>
bool thread_pool<T>::append(T * request) {
    if(request == nullptr) return false;
    queue_locker.lock();
    if(m_workqueue.size() >= m_max_requests) {
        queue_locker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    queue_locker.unlock();
    queue_stat.post();
    return true;
}

template<typename T>
void thread_pool<T>::run() {
    while(!is_stopped) { //只要线程池还有存在的意义，它们就要等待新的任务来临
        queue_stat.wait();
        queue_locker.lock();
        if(m_workqueue.empty()) {
            queue_locker.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        queue_locker.unlock();

        request-> process(); //调用任务类的接口完成具体任务
    }
}

template<typename T>
void* thread_pool<T>::worker(void* arg) {
    thread_pool* pool = (thread_pool*) arg;
    pool->run();
    return pool;
}

#endif