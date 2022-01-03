//这个头文件封装一些信号量、互斥量之类的东西。

#ifndef LOCKER_H
#define LOCKER_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>

//封装信号量的类

class sem{
    public:
    sem(){
	if(sem_init(&m_sem,0,0)!=0){//初始化一个未命名的信号量.第二个参数为0，说明是当前进程的局部信号量.第三个参数为0，说明信号量初值为0.
	    throw std::exception();
	}
    }
    //用外部int初始化sem
    sem(int num){
	if(sem_init(&m_sem,0,num)!=0){
	    throw std::exception();
	}
    }
    //销毁信号量，调用sem_destroy函数。如果不掉用，默认的析构函数只会把m_sem删掉，但无法删除对应的一系列内核资源。
    ~sem(){
	sem_destroy(&m_sem);
    }
    bool wait(){
	return sem_wait(&m_sem)==0;
    }
    bool post(){
	return sem_post(&m_sem)==0;
    }
    private:
	sem_t m_sem;
};

class locker{
    public:
	locker(){
	    if(pthread_mutex_init(&m_mutex,NULL)!=0){
		throw std::exception();
	    }
	}
	~locker(){
	    pthread_mutex_destroy(&m_mutex);
	}
	bool lock(){
	    return pthread_mutex_lock(&m_mutex)==0;
	}
	bool unlock(){
	    return pthread_mutex_unlock(&m_mutex)==0;
	}
	pthread_mutex_t *get(){
	    return &m_mutex;
	}
    private:
	pthread_mutex_t m_mutex;
};

//封装条件变量的类
class cond{
    public:
	cond(){
	    //创建互斥锁
	    if(pthread_mutex_init(&m_mutex,NULL)!=0){
		throw std::exception();
	    }
	    //创建条件变量
	    if(pthread_cond_init(&m_cond,NULL)!=0){
		pthread_mutex_destroy(&m_mutex);
		throw std::exception();
	    }
	}
	~cond(){
	    pthread_mutex_destroy(&m_mutex);
	    pthread_cond_destroy(&m_cond);
	}
	//等待条件变量
	bool wait(pthread_mutex_t *m_mutex){
	    int ret = 0;
	   // pthread_mutex_lock(&m_mutex);
	    ret = pthread_cond_wait(&m_cond,m_mutex);
	   // pthread_mutex_unlock(&m_mutex);
	    return ret == 0;
	}
	//唤醒等待条件变量的线程
	bool signal(){
	    return pthread_cond_signal(&m_cond)==0;
	}
	//广播等待条件变量的线程
	bool broadcast(){
	    return pthread_cond_broadcast(&m_cond)==0;
	}
    private:
	pthread_mutex_t m_mutex;
	pthread_cond_t m_cond;
};
#endif

