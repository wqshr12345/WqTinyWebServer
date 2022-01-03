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
#include "../CGImysql/sql_connection_pool.h"
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
	//这里别忘了加上{},因为这里就直接定义完了...不需要在那个cpp文件里定义了。
	http_conn(){}
	~http_conn(){}

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
	//得到address地址
	sockaddr_in* get_address(){
	    return &m_address;
	}
	void initmysql_result(connection_pool *connPool);
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
	HTTP_CODE parse_content(char* text);
	HTTP_CODE do_request();
	char* get_line() {return m_read_buf+m_start_line;}
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
	//该 http连接的socketfd和对方的端口号ip地址
	int m_sockfd;
	sockaddr_in m_address;
	
	//读缓冲区
	char m_read_buf[READ_BUFFER_SIZE];
	//读缓冲区中已经读入的客户数据的最后一个字节的下一个位置   
	int m_read_idx;
	//当前正在分析的字符在读缓冲区的位置
	int m_checked_idx;
	//当前正在解析的行的起始位置
	int m_start_line;
	//写缓冲区
	char m_write_buf[WRITE_BUFFER_SIZE];
	//写缓冲区中待发送的字节数
	int m_write_idx;
	//主状态机的当前状态
	CHECK_STATE m_check_state;
	//请求方法
	METHOD m_method;
	
	//客户请求的目标文件的完整路径，内容等于doc_root+m_url,doc_root是网站根目录
	char m_real_file[FILENAME_LEN];
	//客户请求的目标文件的文件名
	char* m_url;
	//HTTP协议版本号
	char* m_version;
	//主机名
	char* m_host;
	//HTTP请求体的长度
	int m_content_length;
	//HTTP请求是否要保持连接？
	bool m_linger;

	//客户请求的目标文件被mmap到内存的起始位置
	char* m_file_address;
	//目标文件的状态，通过其可以判断目标文件是否存在、是否为目录、是否可读等
	struct stat m_file_stat;
	//使用writev集中写，所以定义下面两个成员。其中m_iv_count表示被写内存块的数量。
	struct iovec m_iv[2];
	int m_iv_count;
};
#endif	
