#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>

//线程同步机制包装类
#include "../locker/locker.h"

//数据库连接池类
#include "../CGImysql/sql_connection_pool.h"

//线程池类。定义成模板类方便代码复用。
template<typename T>
class threadpool{
    public:
	//构造函数。thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的等待处理的请求数量。
	//另外注意，如果构造函数是用到了默认参数，应该把其放在后面。
	threadpool(connection_pool *connPool,int thread_number = 8,int max_requests = 10000);
	~threadpool();
	//append函数。往请求队列中添加任务。
	bool append(T* request);

    private:
	//工作线程运行的函数
	static void* worker(void* arg);
	void run();

    private:
	int m_thread_number;//线程池中线程数量
	int m_max_requests;//请求队列最多允许的等待处理的请求数量
	pthread_t* m_threads;//描述线程池的数组，大小为m_thread_number。(在进程池中，定义了了一个process类，然后使用了process数组)
	std::list<T*> m_workqueue;//请求队列
	locker m_queuelocker;//保护请求队列的互斥锁
	sem m_queuestat;//判断是否有任务需要处理的信号量
	bool m_stop;//是否结束线程
	connection_pool *m_connPool;//数据库
};

//线程池构造函数。初始化了一些重要参数，同时创建好指定数量的线程
//这里需要初始化指向数据库连接池的指针哇～
template<typename T>
threadpool<T>::threadpool(connection_pool *connPool,int thread_number,int max_requests):m_thread_number(thread_number),m_max_requests(max_requests),m_stop(false),m_threads(NULL),m_connPool(connPool){
    if((thread_number<=0)||(max_requests<=0)){
	throw std::exception();
    }
    //使用new生成一个含有指定数量的线程数组。
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
	throw std::exception();
    }
    //创建指定数量的线程。
    for(int i = 0;i<thread_number;++i){
	printf("create the %dth thread\n",i);
	if(pthread_create(m_threads+i,NULL,worker,this)!=0){
	    delete [] m_threads;
	    throw std::exception();
	}
	if(pthread_detach(m_threads[i])){//pthread_detach可以设置线程为joined状态，此时进程终止之后自动释放资源，无需再调用pthread_join释放。这在web服务器尤其好用，如果这样设置之后，接受新的连接分配给线程之后，不用调用pthread_join善后(这会导致主线程阻塞)，可以放心处理下一个连接
	    delete [] m_threads;
	    throw std::exception();
	}
    }
}

//线程池析构函数。
template<typename T>
threadpool< T >::~threadpool(){
    delete [] m_threads;
    m_stop = true;
}


//向请求队列添加任务函数。(这多么像生产者消费者问题。)有一点不一样，就是这里只用了一个信号量，类似生产者消费者里的full，没有empty。而empty的等待队列用来存放请求队列满了的情况下，还想生产的生产者。源代码也很简单，既然满了，就退出，并不会把你放到等待队列。有机会实现一个双信号量的。
template<typename T>
bool threadpool<T>::append(T* request){
    //操作请求队列的时候一定要加锁，因为这是被所有线程所共享的
    m_queuelocker.lock();
    if(m_workqueue.size()>m_max_requests){
	m_queuelocker.unlock();
	return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return false;
}

//线程的工作函数
template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
	m_queuestat.wait();
	m_queuelocker.lock();
	printf("一个任务正在被线程池处理\n");
	//有个最大的问题：如果m_queuestat为0，也就是待请求队列数量为0，那么wait会一直阻塞啊，所以这段调用好像用不到？(一会写一个测试案例)
	if(m_workqueue.empty()){
	    m_queuelocker.unlock();
	    printf("请求队列为空，消费者无法获得。");
	    continue;
	}
	T* request = m_workqueue.front();
	m_workqueue.pop_front();
	m_queuelocker.unlock();
	if(!request){
	    continue;
	}
	connectionRALL mysqlcon(&request->mysql,m_connPool);
	request->process();
	printf("一个任务已经被处理完了\n");
    }
}
#endif
