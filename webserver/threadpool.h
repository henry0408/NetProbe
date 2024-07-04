#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"

using namespace std;

/* 对于线程池，里面每个线程对应一个任务（由主线程发来），所以利用模板template */

template<typename T> 
class threadpool
{
public:
    //构造线程池，初始化大小为8个线程，最大请求数为10000（请求链表长度）
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    //往任务链表中添加任务
    bool append(T* request);

private:
    //线程数量
    int m_thread_number;

    //线程池的线程的数组(用数组存放所有的线程)
    pthread_t * m_threads;

    //任务请求链表（任务链表）
    list< T* > m_workqueue; 

    //请求链表的最大数量（链表长度）
    int m_max_requests; 

    //保护请求链表的互斥锁（同一时刻，只能有一个线程访问请求链表：主线程发布任务，线程池线程接收任务）
    locker m_queuelocker;  

    //是否有任务需要处理（信号量）：如果有任务要处理，主线程发布任务后会释放信号量（+1），线程池线程会处理线程，取得信号量（-1）
    sem m_queuestat;

    //是否结束线程（如果为true，会结束线程池的所有任务）
    bool m_stop; 
private:
    /*工作线程运行的函数，线程不断从工作任务链表中取出任务，并执行*/
    static void* worker(void* arg);
};

//线程池构造
template<typename T> 
threadpool<T>::threadpool(int thread_num, int max_request_num)
{
    if((thread_num <= 0) || (max_request_num <= 0) ) {
        throw std::exception();
    }
    m_thread_number = thread_num;
    m_max_requests = max_request_num;
    m_stop = false;

    //初始化线程池线程数组
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) {
        throw std::exception();
    }

    //初始化线程池数组中的所有线程，并进行线程分离
    for(int i = 0; i<m_thread_number; i++)
    {
        printf( "create the %dth thread\n", i);

        //创建线程，需要指定线程执行的函数：worker，传入参数为this
        if(pthread_create(m_threads + i, NULL, worker, this ) != 0) {
            delete [] m_threads;
            throw std::exception();
        }

        //设置线程分离
        pthread_detach(pthread_detach( m_threads[i] ));
    }

}

//线程池析构
template< typename T >
threadpool< T >::~threadpool() {
    delete [] m_threads;
    m_stop = true;
}

//添加任务到任务链表函数
template< typename T >
bool threadpool< T >::append( T* request )
{
    // 操作任务链表时一定要加锁，因为它被所有线程共享。
    m_queuelocker.lock();

    //如果超出了链表最大可以接受的值
    if ( m_workqueue.size() > m_max_requests ) {
        m_queuelocker.unlock();
        return false;
    }

    //插入任务到任务链表
    m_workqueue.push_back(request);

    //解锁
    m_queuelocker.unlock();

    //释放信号量（+1）
    m_queuestat.post();
    return true;
}


//线程执行任务函数：线程从任务链表取出任务，然后执行
template< typename T >
void* threadpool< T >::worker(void* arg)
{
    threadpool* pool = ( threadpool* )arg;

    while (!pool->m_stop) {
        //获取信号量（判断任务链表中有没有任务，没有则阻塞）
        pool->m_queuestat.wait();

        //有任务，因为要取出任务，所以先对任务链表加锁
        pool->m_queuelocker.lock();

        //再次判断任务是否还存在（有没有被其他线程取走）
        if ( pool->m_workqueue.empty() ) {
            pool->m_queuelocker.unlock();
            continue;
        }

        //如果任务还在任务链表，则从链表头取出任务
        T* request = pool->m_workqueue.front();
        pool->m_workqueue.pop_front();

        pool->m_queuelocker.unlock();
        if ( !request ) {
            continue;
        }

        //任务执行函数（任务真正的回调函数）
        request->process();
    }
}


#endif