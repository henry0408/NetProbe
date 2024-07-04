#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

/* 定义：互斥锁、条件变量、信号量 */

/* 互斥锁 */
class locker
{
public:
    //初始化互斥锁
    locker()
    {
        if(pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    //销毁锁
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    //加锁
    bool lock()
    {
        return (pthread_mutex_lock(&m_mutex) == 0);
    }
    //解锁
    bool unlock()
    {
        return (pthread_mutex_unlock(&m_mutex) == 0);
    }
    //获取锁
    pthread_mutex_t* getlocker()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};


/* 条件变量 */
class cond
{
public:
    //初始化条件变量
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0) {
            throw std::exception();
        }
    }
    //销毁条件变量
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    //阻塞获取条件变量
    bool wait(pthread_mutex_t *m_mutex) {
        return (pthread_cond_wait(&m_cond, m_mutex) == 0);
    }
    //阻塞获取条件变量（超时时间）
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t) {
        return (pthread_cond_timedwait(&m_cond, m_mutex, &t) == 0);
    }
    //释放条件变量
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
    //释放条件变量（广播）
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

/* 信号量 */
class sem
{
public:
    //初始化信号量
    sem()
    {
        //初始值为0
        if(sem_init( &m_sem, 0, 0 ) != 0 ) {
            throw std::exception();
        }
    }
    //初始化信号量（规定初始值）
    sem(int num) {
        if( sem_init( &m_sem, 0, num ) != 0 ) {
            throw std::exception();
        }
    }
    //销毁信号量
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    //获取信号量（-1）
    bool wait() {
        return sem_wait( &m_sem ) == 0;
    }
    //释放信号量（+1）
    bool post() {
        return sem_post( &m_sem ) == 0;
    }
private:
    sem_t m_sem;
};

#endif