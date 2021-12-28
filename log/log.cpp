#include<string.h>
#include<time.h>
#include<sys/time.h>
#include<stdarg.h>
#include "log.h"
#include<pthread.h>
using namespace std;

//构造函数和析构函数已经在头文件写好


//初始化函数。file_name相当于输出日志到哪个文件，异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name,int log_buf_size,int split_lines,int max_queue_size){
    //如果设置了max_queue_size,则设置为异步，新建一个线程来写日志，不需要主线程写。这其实算某种含义上的proactor。
    if(max_queue_size>=1){
	m_is_async = true;
	m_log_queue = new block_queue<string>(max_queue_size);
	pthread_t tid;
	//flush_log_thread是子线程的回调函数，用来写日志
	pthread_create*(&tid,NULL,flush_log_thread,NULL);
    }
    //输出内容的长度
    m_log_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf,'\0',m_log_buf_size);

    //日志的最大行数
    m_split_lines = split_lines;

    //当前时间
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //从后往前找文件中第一个/的位置。
    const char* p = strrchr(file_name,'/');
    char log_full_name[256] = {0};
    //如果输入的文件名没有/，直接用时间+文件名fime_name作为日志名log_full_name
    if(p == NULL){
	snprintf(log_full_name,255,"%d_%02d_%02d_%s",my_tm.tm_year+1900,my_tm.tm_mday,file_name);
    }
    else{
	//写了些什么勾八东西
	strcpy(log_name,p+1);
	strcpy()
	//to do...	
    }

}

