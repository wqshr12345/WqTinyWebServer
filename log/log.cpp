#include<string.h>
#include<time.h>
#include<sys/time.h>
#include<stdarg.h>
#include "log.h"
#include<pthread.h>
using namespace std;

//构造函数和析构函数已经在头文件写好


//初始化函数。负责新建一个log文件，并想好文件名云云～

//file_name相当于输出日志到哪个文件，异步需要设置阻塞队列的长度，同步不需要设置
bool Log::init(const char *file_name,int log_buf_size,int split_lines,int max_queue_size){
    //如果设置了max_queue_size,则设置为异步，新建一个线程来写日志，不需要主线程写。这其实算某种含义上的proactor。
    if(max_queue_size>=1){
	m_is_async = true;
	m_log_queue = new block_queue<string>(max_queue_size);
	pthread_t tid;
	//flush_log_thread是子线程的回调函数，用来写日志(成员函数不需要使用作用域运算符就可以直接调用静态成员函数flush_log_thread)
	pthread_create(&tid,NULL,flush_log_thread,NULL);
    }
    //初始化写缓冲的大小(写缓冲存储此次日志的具体内容)
    m_log_buf_size = log_buf_size;
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

    //确定输出日志的文件名log_full_name

    //如果输入的文件名没有/，直接用时间+文件名fime_name作为日志名log_full_name
    if(p == NULL){
	snprintf(log_full_name,255,"%d_%02d_%02d_%s",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,file_name);
    }
    else{
	//写了些什么勾八东西
	strcpy(log_name,p+1);
	strncpy(dir_name,file_name,p-file_name+1);
	snprintf(log_full_name,255,"%s%d_%02d_%02d_%s",dir_name,my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,log_name);	
    }
    m_today = my_tm.tm_mday;
    //用a模式打开，如果文件不存在会创建文件。
    m_fp = fopen(log_full_name,"a");
    if(m_fp == NULL){
	return false;
    }
    return true;
}

//在init创建好文件之后，往文件里添加日志内容的函数
void Log::write_log(int level,const char* format,...){
    //获得当前时间
    struct timeval now = {0,0};
    gettimeofday(&now,NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
 
    printf("这是日志内容:%s",format);
    //根据分级写入不同内容
    switch(level){
	case 0:
	    strcpy(s,"[debug]:");
	    break;
	case 1:
	    strcpy(s,"[info]:");
	    break;
	case 2:
	    strcpy(s,"[warn]:");
	    break;
	case 3:
	    strcpy(s,"[error]:");
	    break;
	default:
	    strcpy(s,"[info]:");
	    break;
    }
    //下面涉及对文件的操作，要上锁。
    m_mutex.lock();

    //更新现有行数(因为写一次日志就是增加一行而已)
    m_count++;

    //如果日志不是今天写的，或者日志行数是最大行的倍数，就创建新的日志文件最大行倍数其实就是写入的行数到达了最大行。1倍的时候要准备开启第二个文件，2倍的时候准备开启第三个文件。
    if(m_today!=my_tm.tm_mday || m_count % m_split_lines == 0){
	//新日志文件的文件路径+文件名
	char new_log[256] = {0};
	
	//关闭上一个文件
	fflush(m_fp);
	fclose(m_fp);
 	char tail[16] = {0};	
	//存储文件名中的时间部分
	snprintf(tail,16,"%d_%02d_%02d_",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday);
	//如果时间不是今天，就创造今天的日志，并重置m_count,更新m_today
	if(m_today!=my_tm.tm_mday){
	    snprintf(new_log,255,"%s%s%s",dir_name,tail,log_name);
	    m_today = my_tm.tm_mday;
	    m_count = 0;
	}
	//否则，说明行数到达了最大行，应该在新建的日志后面加后缀
	else{
	    snprintf(new_log,255,"%s%s%s.%lld",dir_name,tail,log_name,m_count/m_split_lines);
	}
	m_fp = fopen(new_log,"a");
    }
    m_mutex.unlock();

    va_list valst;
    va_start(valst,format);

    string log_str;
    //下面涉及对m_buf的访问，所以上锁？？？
    m_mutex.lock();

    //写入日志的具体内容字符串
    int n = snprintf(m_buf,48,"%d-%02d-%02d %02d:%02d:%02d.%06ld %s",my_tm.tm_year+1900,my_tm.tm_mon+1,my_tm.tm_mday,my_tm.tm_hour,my_tm.tm_min,my_tm.tm_sec,now.tv_usec,s);
    int m = vsnprintf(m_buf+n,m_log_buf_size-1,format,valst);
    m_buf[n+m] = '\n';
    m_buf[n+m+1] = '\0';

    log_str = m_buf;

    m_mutex.unlock();

    //如果是异步模式，就把字符串放到阻塞队列。同步模式就加锁直接写。
    if(m_is_async && !m_log_queue->full()){
	m_log_queue->push(log_str);
    }
    else{
	//文件是大家共享的，所以写的时候上锁。
	m_mutex.lock();
	fputs(log_str.c_str(),m_fp);
	m_mutex.unlock();
    } 
    va_end(valst);

}

void Log::flush(void){
    m_mutex.lock();
    fflush(m_fp);
    m_mutex.unlock();

}
