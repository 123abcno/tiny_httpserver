#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <list>
#include <pthread.h>
#include "locker.h"
#include <cstdio>
#include <cstdlib>
#include "http_conn.h"
template<typename T>
class thread_pool{
private:
    /* data */

    //num of thread
    int m_thread_num;
    //max request
    int m_max_request;
    //the dynamic array of thread_pool
    pthread_t *m_threads;
    //request queue
    std::list<T*> m_workqueue;
    //visit request queue
    locker m_workqueue_lock;
    //request queue's sem，init 0
    sem m_workqueue_sem;
    //是否结束线程
    bool m_stop;
public:
    static void*work(void *arg);
    thread_pool(int thread_num=8,int max_requests=1000);
    ~thread_pool();
    bool append(T* request);
    void run();
};

template<typename T>
thread_pool<T>::thread_pool(int thread_num,int max_requests):m_thread_num(thread_num),
    m_max_request(max_requests),m_threads(NULL),m_stop(false){
    if(m_thread_num<=0 || max_requests<=0){
        throw std::exception();
    }

    m_threads=new pthread_t[m_thread_num];
    if(m_threads==NULL){
        throw std::exception();
    }
    //创建线程
    for(int i=0;i<m_thread_num;i++){
        printf("create thread: %d\n",i);

        if(pthread_create(m_threads+i,NULL,work,this)!=0){
            delete[] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i])!=0){//分离
            delete[] m_threads;
            throw std::exception();
        }

    }
    
}

template<typename T>
thread_pool<T> ::~thread_pool(){
    delete[] m_threads;
    m_stop=true;//线程停止
}

//往任务队列添加任务
template<typename T>
bool thread_pool<T>::append(T* request){
    m_workqueue_lock.lock();//访问队列
    if(m_workqueue.size()>=m_max_request){
        m_workqueue_lock.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_workqueue_lock.unlock();
    m_workqueue_sem.post();
    return true;
}

template<typename T>
void* thread_pool<T>::work(void *arg){//从workqueue中取出request
    thread_pool<T> *pool=(thread_pool<T>*)arg;//与视频不同
    pool->run();
}

template<typename T>
void thread_pool<T>::run(){
    while(!m_stop){
        m_workqueue_sem.wait();//是否有请求

        m_workqueue_lock.lock();//访问

        T* request=m_workqueue.front();
        m_workqueue.pop_front();

        m_workqueue_lock.unlock();

        if(!request){
            continue;
        }
        //deal request
        request->process();
    }
}
#endif