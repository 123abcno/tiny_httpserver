#ifndef LOCKER_H
#define LOCKER_H
#include <pthread.h>
#include <exception>
#include <semaphore.h>
/* 
互斥锁
*/
class locker{
private:
    /* data */
    pthread_mutex_t m_mutex;
public:
    locker(/* args */);
    ~locker();
    bool lock();
    bool unlock();
    pthread_mutex_t * get();
};

locker::locker(){
    if(pthread_mutex_init(&m_mutex,NULL)!=0){
        throw std::exception();
    }
}

locker::~locker(){
    pthread_mutex_destroy(&m_mutex);
}

bool locker::lock(){
    return pthread_mutex_lock(&m_mutex);
}

bool locker::unlock(){
    return pthread_mutex_unlock(&m_mutex);
}
pthread_mutex_t * locker::get(){
    return &m_mutex;
}


/* 
条件变量
*/
class cond{
private:
    /* data */
    pthread_cond_t m_cond;
public:
    cond(/* args */);
    ~cond();
    bool wait(pthread_mutex_t *mutex);
    bool singal(pthread_mutex_t *mutex);
};

cond::cond(){
    if(pthread_cond_init(&m_cond,NULL)!=0){
        throw std::exception();
    }
}

cond::~cond(){
    pthread_cond_destroy(&m_cond);
}

bool cond::wait(pthread_mutex_t *mutex){
    return pthread_cond_wait(&m_cond,mutex)==0;
}

bool cond::singal(pthread_mutex_t *mutex){
    return pthread_cond_signal(&m_cond);
}

/*
信号量
*/
class sem{
private:
    /* data */
    sem_t m_sem;
public:
    sem(int n=0);
    ~sem();
    bool wait();
    bool post();
};

sem::sem(int n){
    if(sem_init(&m_sem,0,n)!=0){
        throw std::exception();
    }
}

sem::~sem(){
    sem_destroy(&m_sem);
}

bool sem::wait(){
    return sem_wait(&m_sem)==0;
}

bool sem::post(){
    return sem_post(&m_sem);
}

#endif