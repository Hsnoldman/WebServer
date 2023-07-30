#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

// 线程池
/*
空间换时间,浪费服务器的硬件资源,换取运行效率.
池是一组资源的集合,这组资源在服务器启动之初就被完全创建好并初始化,这称为静态资源.
当服务器进入正式运行阶段,开始处理客户请求的时候,如果它需要相关的资源,可以直接从池中获取,无需动态分配.
当服务器处理完一个客户连接后,可以把相关的资源放回池中,无需执行系统调用释放资源.
*/
template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();

    //向请求队列中插入任务请求
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg); // worker为静态函数
    void run();

private:
    int m_thread_number;         // 线程池中的线程数
    int m_max_requests;          // 请求队列中允许的最大请求数
    pthread_t *m_threads;        // 描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue;  // 工作队列
    locker m_queuelocker;        // 保护请求队列的互斥锁
    sem m_queuestat;             // 信号量，标记是否有任务需要处理
    connection_pool *m_connPool; // 数据库连接池
    int m_actor_model;           // 模型切换
};
//threadpool<http_conn> T为：http_conn
template <typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    // 创建线程池数组
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    // 创建thread_number个线程
    for (int i = 0; i < thread_number; ++i)
    {
        /*
        pthread_create()函数参数
            1.新创建的线程ID指向的内存单元
            2.线程属性，默认为NULL
            3.新创建的线程从worker函数的地址开始运行
            4.默认为NULL。若上述worker函数需要参数，将参数放入结构中并将地址作为arg传入

            // worker为线程所执行的函数，必须为静态函数
               但静态函数无法访问类中的非静态成员变量，因此需要给worker函数传入this指针，以此来访问类中的成员变量
        */
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }

        /*
        线程分离就是当线程被设置为分离状态后，线程结束时
        它的资源会被系统自动的回收，而不再需要在其它线程中对其进行 pthread_join() 操作
        pthread_detach()返回值 成功：0；失败：错误号
        */
        // 设置线程分离
        if (pthread_detach(m_threads[i])) // 若失败，返回值不为0，则执行if函数体
        {
            delete[] m_threads;     // 释放数组
            throw std::exception(); // 抛出异常
        }
    }
}

// 析构函数
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

// 往队列中添加任务
/*要配合锁执行*/
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock(); // 上锁
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock(); // 解锁
        return false;
    }
    request->m_state = state;       // 请求状态
    m_workqueue.push_back(request); // 往工作队列中加入请求
    m_queuelocker.unlock();         // 解锁
    m_queuestat.post();             // 信号量+1
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

// 工作函数
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg; // 获取传入的this指针
    pool->run();
    return pool;
}

// 让线程运行起来
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait();   // 获取信号量，如果为0则阻塞，依次来判断能否继续执行
        m_queuelocker.lock(); // 上锁
        // 判断工作队列是否为空
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front(); // 获取一个任务
        m_workqueue.pop_front();          // 从队列中删除该任务
        m_queuelocker.unlock();           // 解锁

        if (!request)
            continue;

        // 选择模型 0:Proactor  1:Reactor
        /*结合http_conn.h和http_conn.cpp文件来看*/

        //Reactor模式
        if (1 == m_actor_model)
        {
            // m_state 读：0 写：1
            //读请求
            if (0 == request->m_state) 
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            //写请求
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        //Proactor模式
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
