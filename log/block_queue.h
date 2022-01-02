//使用循环数组实现的阻塞队列,m_back = (m_back+1)%m_max_size;

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include<iostream>
#include<stdlib.h>
#include<pthread.h>
#include<sys/time.h>
#include"../locker/locker.h"

using namespace std;

template <typename T>
class block_queue{
    public:
	block_queue(int max_size = 1000){
	    if(max_size<=0){
		exit(-1);
	    }
	    //队列的最大数量
	    m_max_size = max_size;
	    //队列(用new生成的数组实现)
	    m_array = new T[max_size];
	    m_size = 0;
	    m_front = -1;
	    m_back = -1;
	}
	void clear(){
	    m_mutex.lock();
	    m_size = 0;
	    m_front = -1;
	    m_back = -1;
	    m_mutex.unlock();
	}
	~block_queue(){
	    m_mutex.lock();
	    if(m_array!=NULL)
		delete [] m_array;
	    m_mutex.unlock();
	}
	//判断队列是否满了(为什么这也要上锁？)
	bool full(){
	    m_mutex.lock();
	    if(m_size>=m_max_size){
		m_mutex.unlock();
		return true;
	    }
	    m_mutex.unlock();
	    return false;
	}
	//判断队列是否为空
	bool empty(){
	    m_mutex.lock();
	    if(0==m_size){
		m_mutex.unlock();
		return true;
	    }
	    m_mutex.unlock();
	    return false;
	}
	//返回队首元素
	bool front(T& value){
	    m_mutex.lock();
	    if(0==m_size){
		m_mutex.unlock();
		return false;
	    }
	    value = m_array[m_front];
	    m_mutex.unlock();
	    return true;
	}
	//返回队尾元素
	bool back(T &value){
	    m_mutex.lock();
	    if(0==m_size){
		m_mutex.unlock();
		return false;
	    }
	    value = m_array[m_back];
	    m_mutex.unlock();
	    return true;
	}
	int size(){
	    int tmp = 0;
	    m_mutex.lock();
	    tmp = m_size;
	    m_mutex.unlock();
	    return tmp;
	}
	int max_size(){
	    int tmp = 0;
	    m_mutex.lock();
	    tmp = m_max_size;
	    m_mutex.unlock();
	    return tmp;
	}
	//往队列添加元素，需要先将所有使用队列的线程唤醒(所以这种信号量不是单纯+，而是维护了一个队列的??)
	bool push(const T& item){
	    m_mutex.lock();
	    if(m_size>=m_max_size){
		m_cond.broadcast();
		m_mutex.unlock();
		return false;
	    }
	    m_back = (m_back+1)%m_max_size;
	    m_array[m_back] = item;
	    m_size++;
	    //添加之后广播告知线程？
	    m_cond.broadcast();
	    m_mutex.unlock();
	    return true;
	}
	//从队列取出元素
	bool pop(T &item){
	    m_mutex.lock();
	    while(m_size<=0){
		if(!m_cond.wait(m_mutex.get())){
		    m_mutex.unlock();
		    return false;
		}
	    }
	m_front = (m_front+1)%m_max_size;
	item = m_array[m_front];
	m_size--;
	m_mutex.unlock();
	return true;
	}
    private:
	locker m_mutex;
	cond m_cond;
	
	T *m_array;
	int m_size;
	int m_max_size;
	int m_front;
	int m_back;
};
#endif
