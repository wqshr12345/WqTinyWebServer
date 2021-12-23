#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include "../locker/locker.h"

//线程池的模板参数类，用于封装对逻辑任务的处理。
class http_conn{
    public:
    //文件名的最大长度
    static const int FILENAME_LEN = 200;
    //读缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    //写缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;
    //HTTP请求方法
    enum METHOD {GET = 0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATCH};
    //主状态机所处的状态(当前正在处理请求行、当前正在处理头部字段、当前正在处理content字段)
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE = 0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};
    //服务器处理HHTP请求产生的可能结果
    enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION};
    //从状态机状态，也是行的读取状态(读到完整行、行出错、行不完整)
    enum LINE_STATUS {LINE_OK = 0,LINE_BAD,LINE_OPEN};
    
    public:
	http_conn();
	~http_conn();

    public:
	//初始化新的连接
	void init(int sockfd,const sockaddr_in& addr);
	//关闭连接
	void close_conn(bool read_close = true);
	//处理客户请求
	void process();
	//非阻塞读操作
	bool read();
	//非阻塞写操作
	bool write();

    private:
	//初始化连接
	void init();
	//解析HTTP请求
	HTTP_CODE process_read();
	//填充HTTP应答
	bool process_write(HTTP_CODE ret);
	//被process_read调用以分析HTTP请求
	HTTP_CODE parse_request_line(char* text);
	HTTP_CODE parse_headers(char* text);
	HTTP_CODE PARSE_content(char* text);
	char* get_line() {return m_read_buf+m_start_line};
	LINE_STATUS parse_line();
	
	//被process_write调用以完成HTTP应答
	void unmap();
	bool add_response(const char* format,...);
	bool add_content(const char* content);
	bool add_status_line(int status,const char* title);
	bool add_headers(int content_length);
	bool add_content_length(int content_length);
	bool add_linger();
	bool add_blank_line();

	public:
	    //主线程使用一个epoll完成对监听socket和连接socket的监听，子线程只负责处理工作逻辑，所以整个程序只有一个epollfd，设置为静态static。
	    static int m_epollfd;
	    //统计用户数量
	    static int m_user_count;
	private:
	//to do   
}	
