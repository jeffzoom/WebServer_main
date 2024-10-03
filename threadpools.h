/*
 * @version: 
 * @Author: zsq 1363759476@qq.com
 * @Date: 2023-04-03 15:28:19
 * @LastEditors: zsq 1363759476@qq.com
 * @LastEditTime: 2024-07-17 19:41:53
 * @FilePath: /Linux_nc/WebServer/myWebServer/threadpools.h
 * @Descripttion: 
 */

#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

#include "logger.h"
#include "locker.h"

using namespace std;

/*
    threadpool使用说明
    threadpool<YourTaskClass> pool(8, 10000);   // 创建线程池实例
    YourTaskClass* task = new YourTaskClass();  // 创建任务实例
    pool.append(task);                          // 将任务添加到线程池的工作队列中
    注意！YourTaskClass是任务类，需要包含一个process()回调函数用于处理任务
*/

// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类
template <typename T>
class threadpool {
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void* worker(void* arg); // 这个回调函数得是静态的，必须是静态的
    void run();

private:
    int m_thread_number;    // 线程的数量
    int m_max_requests;     // 请求队列中最多允许的、等待处理的请求的数量
    pthread_t * m_threads;  // 描述线程池的数组，大小为m_thread_number    
    bool m_stop;            // 是否结束线程
    std::list<T*> m_workqueue;  // 请求队列
    locker m_queuelocker;   // 互斥锁类 // 保护请求队列的互斥锁
    sem m_queuestat;        // 信号量类 // 是否有任务需要处理
};

// 构造函数
template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) : 
    m_thread_number(thread_number), m_max_requests(max_requests), 
    m_stop(false), m_threads(nullptr) {

    if ((max_requests <= 0) || (max_requests <= 0)) {  // 系统鲁棒性更强
        throw std::exception();
    }

    m_threads = new pthread_t[thread_number];
    if (!m_threads) {   // 系统鲁棒性更强
        throw std::exception();
    }

    // 创建thread_number 个线程，并将他们设置为脱离线程，线程分离
    // 当子线程用完之后自己释放资源
    for (int i = 0; i < thread_number; i++) {

        printf( "create the %dth thread\n", i);
        LOG_INFO("create the %dth thread", i);

        if(pthread_create(m_threads + i, NULL, worker, this) != 0) { // 创建thread_number个线程
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])) {      // 设置为脱离线程，线程分离
            delete[] m_threads;
            throw std::exception();
        }
    }
}

// 析构函数
template <typename T>
threadpool<T>::~threadpool() {
    delete [] m_threads;    // 释放资源
    m_stop = true;
}

// 这个回调函数得是静态的，必须是静态的
template <typename T>
void* threadpool<T>::worker(void* arg) {    // 这个回调函数得是静态的，必须是静态的
    printf("worker\n");
    threadpool * pool = (threadpool *)arg;
    pool->run();
    return pool; 
}

// 回调函数嵌套
template <typename T>
void threadpool<T>::run() {         // 回调函数嵌套

    printf("run\n");
    // 下面相当于是muduo网络库中的EventLoop事件循环
    while (!m_stop) {
        // 调用append()函数后，m_queuestat.post()会增加信号量
        m_queuestat.wait();         // 对信号量加锁，调用一次信号量的值-1，如果值为0，就阻塞，
                                    // 判断有没有任务需要做
        m_queuelocker.lock();       // 加锁，多线程就要考虑数据安全，线程同步
        if (m_workqueue.empty()) {  // 请求队列为空则为1
            m_queuelocker.unlock(); // 解锁
            continue;
        }

        // 请求队列操作
        T* request = m_workqueue.front();   //返回请求队列首元素的引用
        m_workqueue.pop_front();    // 删除第一个
        m_queuelocker.unlock();
        if (!request) {
            continue;
        }
        
        request->process();         // 做任务，任务的函数先定义为process，T*的process()函数，也就是http_conn *的process()函数
                                    // 开始处理数据，注意！T模板类一定要定义process()函数，不然没有任务处理函数process()会报错
    }
}

// 往工作队列里面添加任务
template <typename T>
bool threadpool<T>::append(T* request) { // 往工作队列里面添加任务

    printf("Append, Add tasks to work queue\n");
    LOG_INFO("Append, Add tasks to work queue");

    // 操作工作队列时一定要加锁，因为它被所有线程共享。
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();     // 增加信号量，m_queuestat信号量+1
    return true;
}

#endif
