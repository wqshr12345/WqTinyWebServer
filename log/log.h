#ifndef LOG_H
#define LOG_H

#include<stdio.h>
#include<iostream>
#include<string>
#include<stdarg.h>
#include<pthread.h>
#include "block_queue.h"

using namespace std;

class Log{
    //使用函数内局部静态对象实现的懒汉版单例模式
    public:
	//由于不是c++11版本，编译器无法保证内部静态变量的线程安全性，所以这里要加锁。
	static Log* get_instance(){
	    pthread_mutex_lock(&lock);
	    static sigle obj;
	    pthread_mutex_unlock(&lock);
	    return &obj;
	}
	
	static void *flush_log_thread(void *args){
	    Log::get_instance()->async_write_log();
	}

	//初始化参数
	bool init(const char* file_name,int log_buf_size = 8192,int split_lines = 5000000,int max_queue_size = 0);
	//
	void write_log(int level,const char* format,...);

	void flush(void);

    private:
        static pthread_mutex_t lock;
        Log(){
            pthread_mutex_init(&lock,NULL);
	    m_count = 0;
	    m_is_async = false;
        }
	//虚函数版本的Log，这样这个类的对象清除的时候会根据其动态类型找析构函数。
        virtual  ~Log(){
	    if(m_fp!=NULL){
		fclose(m_fp);
	    }
	}
	void *async_write_log(){
	    string single_log;
	    //从阻塞队列中取出一个日志string，写入文件
	    while(m_log_queue->pop(single_log)){
		m_mutex.lock();
		fputs(single_log.c_str(),m_fp);
		m_mutex.unlock();
	    }
	}
    private:
	char dir_name[128];//路径名
	char log_name[128];//log文件名
	int m_split_lines;//日志最大行数
	int m_log_buf_size;//日志缓冲区大小
	long long m_count;//日志行数记录
	int m_today;//当前时间
	FILE *m_fp;//打开log的指针
	char *m_buf;
	block_queue<string> *m_log_queue;//阻塞队列
	bool m_is_async;//是否同步标志位
	locker m_mutex;
};
#define LOG_DEBUG(format,...)Log::get_instance()->write_log(0,format,##_VA_ARGS_)
#define LOG_INFO(format,...)Log::get_instance()->write_log(1,format,##_VA_ARGS_)
#define LOG_WARN(format,...)Log::get_instance()->write_log(2,format,##_VA_ARGS_)
#define LOG_ERROR(format,...)Log::get_instance()->write_log(3,format,##_VA_ARGS_)
#endif
