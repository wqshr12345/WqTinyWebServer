#include "http_conn.h"
//#include "../log/log.h"
#include<map>
//#include<mysql/mysql.h>
#include<fstream>

#define connfdLT //LT模式的连接socket
//#define connfdET//ET模式的连接socket

#define listenfdLT //LT模式的监听socket
//#define listenfdET //ET模式的监听socket

//HTTP响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

//网站的根目录（to do）
const char* doc_root = "/";

//设置某fd为非阻塞(为了方便使用ET模式)
int setnonblocking(int fd){
    int old_option = fcntl(F_GETFL,fd);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

//epoll相关函数
//向epoll内核事件表注册事件(根据define判断是用ET还是LT)
void addfd(int epollfd,int fd,bool one_shot,bool et){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN|EPOLLRDHUP;
    if(et){
	event.events |= EPOLLET;
	setnonblocking(fd);
    }
    if(one_shot){
	event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
}

//从epoll内核事件表中删除事件
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}
//修改epoll内核事件表中的已注册事件
void modfd(int epollfd,int fd,int ev,bool et){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev|EPOLLONESHOT|EPOLLRDHUP;
    if(et){
	event.events |= EPOLLET;
    }
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int http_conn::m_user_count = 0;//这属于类的static静态成员变量，所有对象共享一份。
int http_conn::m_epollfd = -1;

//下面是关于http连接操作的函数
//关闭某个连接
void http_conn::close_conn(bool real_close){
    if(real_close&&(m_sockfd!=-1)){
	removefd(m_epollfd,m_sockfd);
	m_sockfd = -1;
	m_user_count--;//关闭一个连接，将客户数量-1.
    }
}

//初始化连接，本质上使用addfd将fd添加注册。
void http_conn::init(int sockfd,const sockaddr_in &addr){
    m_sockfd = sockfd;
    m_address = addr;
    //如下两行为了避免TIME_WAIT状态，仅仅为了调试方便，关服务器后不用换个端口开启。（不对啊，如果是那样，应该是在主线程的bind前面设置。这里设置的是连接socket，有勾八用？）
    int reuse = 1;
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    //在这里设置了ET和oneshot都为true！！！！
    addfd(m_epollfd,sockfd,true,true);
    m_user_count ++;
    init();
}

//初始化连接的子函数，本质上就是初始化各种参数。
void http_conn::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_real_file,'\0',FILENAME_LEN);
}

//从状态机,用于解析一行内容,判断是否为完整行。
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    //checked_index指向buffer中正在分析的字节，read_index指向buffer中客户数据的>尾部的下一字节。
    for(;m_checked_idx<m_read_idx;++m_checked_idx){
        temp = m_read_buf[m_checked_idx];
        if(temp == '\r'){
            if((m_checked_idx+1)==m_read_idx){
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx+1]=='\n'){
                m_read_buf[m_checked_idx++]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp=='\n'){
            if((m_checked_idx>1)&&m_read_buf[m_checked_idx-1]=='\r'){
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
             }
        return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//循环读取socket中的数据。这是主线程调用的方法，用于把数据放到buf里。
bool http_conn::read(){
    if(m_read_idx>=READ_BUFFER_SIZE){
	return false;
    }
    int bytes_read = 0;
    while(true){
	bytes_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
	if(bytes_read == -1){
	    if(errno == EAGAIN || errno == EWOULDBLOCK){
		//只有这种情况是socket无数据可读，也就是已经recv完全了，可以return true。
		break;
	    }
	    return false;
	}
	else if(bytes_read == 0){
	    return false;
	}
	m_read_idx +=bytes_read;
    }
    return true;
}

//分析请求行 诸如GET http://www.baidu.com/index.html HTTP/1.0
//吐槽：下面的方法里分割字符串的方法也太丑陋难读了吧。
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    char* url = strpbrk(text," \t");
    if(!url){
        return BAD_REQUEST;
    }
    *url++ = '\0';//这样之后原字符串相当于 GET\0http...,url这时候指向h。这么说原来的temp就是GET了，因为遇到\0结束字符串读取，相当于原字符串分成了俩字符串，temp>和url。

    char* method = text;
    if(strcasecmp(method,"GET")==0){
        printf("The request method is GET\n");
    }
    else{
        return BAD_REQUEST;
    }
    url += strspn(url," \t");//跳过url字符串中的 \t字段。
    char* version = strpbrk(url," \t");
    if(!version){
        return BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn(version," \t");
    if(strcasecmp(version,"HTTP/1.1")!=0){
        return BAD_REQUEST;
    }
    if(strncasecmp(url,"http://",7)==0){
        url+=7;
        url = strchr(url,'/');
    }
    if(!url || url[0]!='/'){
        return BAD_REQUEST;
    }
    printf("The request URL is: %s\n",url);
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//分析头部字段 诸如 User-Agent Wget/1.12 (linux-gnu)  Host:www.baidu.com  Connection:close

http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    //遇到空行，说明头部解析到最后一行了，解析完毕。
    if(text[0]=='\0'){
	//头部解析完毕，判断是否有消息体，如果有，需要把状态转移
	if(m_content_length!=0){
	    m_check_state = CHECK_STATE_CONTENT;
	    return NO_REQUEST;
	}
	//否则说明的到了一个完整的HTTP请求。
        return GET_REQUEST;
    }
    //处理Connection字段
    else if(strncasecmp(text,"Connection:",11)==0){
	text +=11;
	text +=strspn(text,"\t");
	if(strcasecmp(text,"keep-alive")==0){
	    m_linger = true;
	}
    }
    //处理Content-Length字段
    else if(strncasecmp(text,"Content-Length:",15)==0){
	text += 15;
	text += strspn(text,"\t");
	m_content_length = atol(text);
    }
    //处理HOST字段
    else if(strncasecmp(text,"Host:",5)==0){
        text += 5;
        text += strspn(text," \t");
	m_host = text;
    }
    //其他字段不处理
    else{
        printf("Sorry!I can not handle this header\n");
    }
    return NO_REQUEST;
}

//分析消息体字段。仅仅判断其是否被完全读入
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if(m_read_idx>=(m_content_length+m_checked_idx)){
	text[m_content_length] = '\0';
	return GET_REQUEST;
    }
    return NO_REQUEST;
}

//

//正式分析HTTP请求的函数,即主状态机
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;
    //当当前读取了一行的时候，进入后面，
    while(((m_check_state = CHECK_STATE_CONTENT)&&(line_status==LINE_OK))||((line_status = parse_line())==LINE_OK)){
        text = get_line();//这里实际上就是：buffer+startline，后者是这一行在buffer中的起始位置
        m_start_line = m_checked_idx;//读完这一行，就把下一次起始位置设置为当前读到的最新地方，也就是该行末尾。
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST){
                    return do_request();
                }
                break;
            }
	    case CHECK_STATE_CONTENT:
	    {
	        ret = parse_content(text);
		if(ret == GET_REQUEST){
		    return do_request();
		}
		line_status = LINE_OPEN;
		break;
	    }
            default:{
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}


//得到一个完整的HTTP请求时，需要分析这个请求的具体内容。

http_conn::HTTP_CODE http_conn::do_request(){
    //把doc_root复制到m_real_file指向的地方，准备作为目标文件的地址
    strcpy(m_real_file,doc_root);
    int len = strlen(doc_root);
    //在doc_root后面跟着复制m_url
    strncpy(m_real_file+len,m_url,FILENAME_LEN-len-1);
    //m_file_stat中存储目标文件的一些属性，比如大小、是否为目录等
    if(stat(m_real_file,&m_file_stat)<0){
	return NO_RESOURCE;
    }
    //st_mode判断当前用户是否有读目标文件的权限
    if(!(m_file_stat.st_mode&S_IROTH)){
	return FORBIDDEN_REQUEST;
    }
    //S_ISDIR判断目标文件是否为目录
    if(S_ISDIR(m_file_stat.st_mode)){
	return BAD_REQUEST;
    }
    //这里将目标文件通过mmap映射到内存的一块空间...why不直接read然后发socket？或者用sendfile直接把文件往socket写。
    int fd = open(m_real_file,O_RDONLY);
    m_file_address = (char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    close(fd);
    return FILE_REQUEST;
}

//对内存映射区进行munmap操作，应该是释放内存。
void http_conn::unmap(){
    if(m_file_address){
	munmap(m_file_address,m_file_stat.st_size);
	m_file_address = 0;
   }
}

//写HTTP响应
bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;//已经发送了多少字节。
    int bytes_to_send = m_write_idx;//还剩多少字节没发送。
    //什么意思？
    if(bytes_to_send == 0){
	modfd(m_epollfd,m_sockfd,EPOLLIN,true);
	init();
	return true;
    }
    while(1){
	//writev是集中写，把多块内存所在的地方统一写到目标fd里。前面的do_requset已经把目标文件写到一块内存里了。响应的响应行、头部字段和空行在另一块内存中。
	temp = writev(m_sockfd,m_iv,m_iv_count);
	//如果writev失败，说明集中写失败。why？可能是TCP写缓冲没有空间。这里要重新注册EPOLLOUT事件，因为采用了ET模式，只会返回一次。
	if(temp<=-1){
	    if(errno == EAGAIN){
		modfd(m_epollfd,m_sockfd,EPOLLOUT,true);
		return true;
	    }
	unmap();
	return false;
	}
	bytes_to_send -= temp;
	bytes_have_send += temp;
	if(bytes_to_send<bytes_have_send){
	    //发送HTTP成功？
	    unmap();
	    //如果是长连接，说明这次http请求处理结束后不close fd，
	    if(m_linger){
		init();
		modfd(m_epollfd,m_sockfd,EPOLLIN,true);
		return true;
	    }
	    else{
		modfd(m_epollfd,m_sockfd,EPOLLIN,true);
		return false;
	    }
	}
    }
}

//往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format,...){
    if(m_write_idx>=WRITE_BUFFER_SIZE){
	return false;
    }
    va_list arg_list;
    va_start(arg_list,format);
    int len = vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx)){
	return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}


//添加状态行
bool http_conn::add_status_line(int status,const char* title){
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

//添加头部字段
bool http_conn::add_headers(int content_len){
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length:%d\r\n",content_len);
}

bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n",(m_linger==true)?"keep-live":"close");
}

bool http_conn::add_blank_line(){
    return add_response("%s","\r\n");
}

bool http_conn::add_content(const char* content){
    return add_response("%s",content);
}

//根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
	case INTERNAL_ERROR:{
	    add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
	    break;
	}
	case BAD_REQUEST:{
	    add_status_line(400,error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)){
                return false;
            }
	    break;
	}
	case NO_RESOURCE:{
	    add_status_line(404,error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)){
                return false;
            }
	    break;
	}
	case FORBIDDEN_REQUEST:{
	    add_status_line(403,error_403_title);
	    add_headers(strlen(error_403_form));
	    if(!add_content(error_403_form)){
		return false;
	    }
	    break;
	}
	case FILE_REQUEST:{
	    add_status_line(200,ok_200_title);
	    if(m_file_stat.st_size != 0){
		add_headers(m_file_stat.st_size);
		m_iv[0].iov_base = m_write_buf;
		m_iv[0].iov_len = m_write_idx;
		m_iv[1].iov_base = m_file_address;
		m_iv[1].iov_len = m_file_stat.st_size;
		m_iv_count = 2;
		return true;
	    }
	    else{
		const char* ok_string = "<html><body></body></html>";
		add_headers(strlen(ok_string));\
		if(!add_content(ok_string)){
		    return false;
		}
	    }
	}
	default:{
	    return false;
	}
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

//线程池中的工作线程调用，这是处理HTTP的入口函数
void http_conn::process(){
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST){
	modfd(m_epollfd,m_sockfd,EPOLLIN,true);
   	return;
     }
    bool write_ret = process_write(read_ret);
    if(!write_ret){
	close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT,true);
}
