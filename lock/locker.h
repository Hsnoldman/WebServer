#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h> //信号量头文件

/*
信号量是一种特殊的变量，访问具有原子性，用于解决进程或线程间共享资源引发的同步问题。

用户态进程对 sem 信号量可以有以下两种操作：
    等待信号量：当信号量值为 0 时，程序等待；当信号量值大于 0 时，信号量减 1，程序继续运行。
    发送信号量：将信号量值加 1
通过对信号量的控制，从而实现共享资源的顺序访问。
*/
// 信号量类 semaphore
class sem
{
public:
    /*
    初始化信号量：int sem_init(sem_t *sem, int pshared, unsigned int value);
    -pshared 控制信号量的类型：
        值为 0 代表该信号量用于多线程间的同步
        值如果大于 0 表示可以共享，用于多个相关进程间的同步
    -value
        表示可用的资源的数目，即信号灯的数目
    s-返回值
        成功时返回 0；错误时，返回 -1，并把 errno 设置为合适的值。
    */
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    // 设置信号量数量
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    // 等待信号量
    bool wait()
    {
        /*
        sem_wait 是一个阻塞的函数，测试所指定信号量的值，它的操作是原子的。
            若 sem value > 0，则该信号量值减去 1 并立即返回。
            若sem value = 0，则阻塞直到 sem value > 0，此时立即减去 1，然后返回。
        */
        return sem_wait(&m_sem) == 0;
    }
    // 增加信号量
    bool post()
    {
        /*
        sem_post 把指定的信号量 sem 的值加 1，唤醒正在等待该信号量的任意线程。
        */
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem; // 创建信号量成员
};

// 互斥锁类 locker
class locker
{
public:
    locker()
    {
        // 互斥锁的初始化
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        { // pthread_mutexattr_init() 函数成功完成之后会返回零，其他任何返回值都表示出现了错误。
            throw std::exception();
        }
    }
    // 析构，销毁锁
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }

    // 加锁
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    // 解锁
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    // 获取互斥量（成员）
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex; // 创建互斥锁成员
};

/*
条件变量是利用线程间共享的全局变量进行同步的一种机制。
    主要包括两个动作：
        一个线程等待”条件变量的条件成立”而挂起；
        另一个线程使”条件成立”（给出条件成立信号）。
为了防止竞争，条件变量的使用总是和一个互斥锁结合在一起。

使用条件变量可以以原子方式阻塞线程，直到某个特定条件为真为止。
条件变量始终与互斥锁一起使用，对条件的测试是在互斥锁（互斥）的保护下进行的。
*/

// 条件变量类 condition variable
/*
判断队列中是否有数据
*/
class cond
{
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            // pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }

    ~cond()
    {
        pthread_cond_destroy(&m_cond); // 删除条件变量
    }

    /*
    pthread_cond_wait()
    当线程调用这个函数的时候，当前线程就会被加入到条件变量中，并阻塞等待。
    第一个参数是要加入的条件变量，第二个参数是互斥锁；
    调用成功返回0，调用失败返回一个错误码
    */
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);

        ret = pthread_cond_wait(&m_cond, m_mutex); // pthread_cond_wait 函数成功返回0；任何其他返回值都表示错误
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal()
    {
        /*
        pthread_cond_signal()
        加入到条件变量中的线程会被挂起，这个函数被调用的时候，让条件变量(等待队列) 中排在队首的线程取消挂起。
        其实就是唤醒排在队首的线程。
        */
        return pthread_cond_signal(&m_cond) == 0;
    }

    // 将所有线程都唤醒
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    // static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif
