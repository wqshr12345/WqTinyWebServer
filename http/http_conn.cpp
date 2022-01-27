#include "http_conn.h"
#include "../log/log.h"
#include<map>
#include<mysql/mysql.h>
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
const char* doc_root = "/home/wangqian/GitHub/WqTinyWebServer/root";


//有一说一，在这种地方定义的变量，是进程里的全局变量，跟类没关系了。
//存储用户名和密码的对应关系
map<string,string> users;
//后面数据库操作时上的锁
locker m_lock;
//当前http_conn对象从数据库中得到关于user的数据库信息，准备应对即将到来的http请求。
void http_conn::initmysql_result(connection_pool *connPool){
    //从连接池获取一个连接
    MYSQL *mysql = NULL;
    connectionRALL mysqlcon(&mysql,connPool);
    //sql语句读数据库
    if(mysql_query(mysql,"SELECT username,passwd FROM user")){
	LOG_ERROR("select error:%s\n",mysql_error(mysql));
    }
    //检验表中完整结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_field(result);

    //从结果集中获取所有的用户名和密码，存入map
    while(MYSQL_ROW row = mysql_fetch_row(result)){
	string temp1(row[0]);
	string temp2(row[1]);
	users[temp1] = temp2;
    }
}


//设置某fd为非阻塞(为了方便使用ET模式)
int setnonblocking(int fd){
    //一开始F_GETFL和fd反了，所以我也不知道这个old_option获得了什么鬼东西～
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

//epoll相关函数
//向epoll内核事件表注册事件(根据define判断是用ET还是LT)
void addfd(int epollfd,int fd,bool one_shot){
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN|EPOLLRDHUP|EPOLLET;
    if(one_shot){
	event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
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
    //在这里设置了oneshot为true！！！！
    addfd(m_epollfd,sockfd,true);
    m_user_count ++;
    init();
}

//初始化连接的子函数，本质上就是初始化各种参数。
void http_conn::init(){
    mysql = NULL;
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
            if((m_checked_idx>1)&&(m_read_buf[m_checked_idx-1]=='\r')){
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
    //因为使用了ET模式，所以读的时候要循环读取。
    while(true){
	bytes_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
	if(bytes_read == -1){
	    if(errno == EAGAIN || errno == EWOULDBLOCK){
		//只有这种情况是socket无数据可读，也就是已经recv完全了，可以return true。为什么不是return false？因为这个服务器还要接着往这个socket写响应，不能关掉这个socket。
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
    m_url = strpbrk(text," \t");
    if(!m_url){
        return BAD_REQUEST;
    }
    *m_url++ = '\0';//这样之后原字符串相当于 GET\0http...,url这时候指向h。这么说原来的temp就是GET了，因为遇到\0结束字符串读取，相当于原字符串分成了俩字符串，temp>和url。

    char* method = text;
    if(strcasecmp(method,"GET")==0){
        printf("The request method is GET\n");
	m_method = GET;
    }
    else if(strcasecmp(method,"POST")==0){
	m_method = POST;
	//如果是post请求，就把cgi标志位设为1
	cgi = 1;
    }
    else{
        return BAD_REQUEST;
    }
    m_url += strspn(m_url," \t");//跳过url字符串中的 \t字段。
    m_version = strpbrk(m_url," \t");
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version," \t");
    if(strcasecmp(m_version,"HTTP/1.1")!=0){
        return BAD_REQUEST;
    }
    //m_url现在是http://baidu.com/index.html 之类。前面的http://是没有意义的，应该去掉.去掉之后剩下baidu.com/index.html这类。还应该让m_url自己跳到index.html，用strchr。
    if(strncasecmp(m_url,"http://",7)==0){
        m_url+=7;
        m_url = strchr(m_url,'/');
    }
    if(strncasecmp(m_url,"https://",8)==0){
	m_url+=8;
	m_url = strchr(m_url,'/');
    }
    if(!m_url || m_url[0]!='/'){
        return BAD_REQUEST;
    }
    //当url没有指定访问界面时，应该访问一个默认界面。这个界面自己设置。java服务器中一般是index.html
    if(strlen(m_url)==1)
	strcat(m_url,"judge.html");
    printf("The request URL is: %s\n",m_url);
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
	LOG_INFO("oop!unknow header:%s",text);
       //printf("Sorry!I can not handle this header\n");
    }
    return NO_REQUEST;
}

//分析消息体字段。如果是get的话，消息体肯定为空。如果是post，消息体有内容，需要读到一块内存。
//另外分析消息体肯定是http请求的最后，所以要根据根据read_idx和checked_idx的差判断是否读取完成。
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if(m_read_idx>=(m_content_length+m_checked_idx)){
	text[m_content_length] = '\0';
	m_string = text;
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
    //两种情况会进入循环。第一种是完整读取了一行；第二种是虽然没有完整读取一行，但是上一行是完整的一行，而且当前正在读取content字段。
    while(((m_check_state == CHECK_STATE_CONTENT)&&(line_status==LINE_OK))||((line_status = parse_line())==LINE_OK)){
        text = get_line();//这里实际上就是：buffer+startline，后者是这一行在buffer中的起始位置
        m_start_line = m_checked_idx;//读完这一行，就把下一次起始位置设置为当前读到的最新地方，也就是该行末尾。
	LOG_INFO("this line is:%s",text);
	Log::get_instance()->flush();
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


//得到一个完整的HTTP请求时，需要分析这个请求的具体内容.根据是post还是get，做出不同的反应。如果是get，直接就在m_real_file的地方设置好请求文件路径，去那个路径读取文件内容再返回就好了；如果是post，就不是请求一个文件了，而是可能有一些操作，需要自定义。

http_conn::HTTP_CODE http_conn::do_request(){
    //把doc_root复制到m_real_file指向的地方，准备作为目标文件的地址
    strcpy(m_real_file,doc_root);
    int len = strlen(doc_root);
    //p指代的就是真实请求地址，比如是/index.html(get下) 比如是/2(post下)
    const char *p = strchr(m_url,'/');


//为2说明是点击注册按钮后客户端发来的、为3说明是点击登录按钮后客户端发来的、为0说明是跳转到注册界面、为1说明是跳转到登录界面、为5说明是跳转到图画界面、为6说明是跳转到video界面、为7说明是跳转到fans界面。

    //判断当前request是get还是post.如果是post，那么是登录or注册？
    if(cgi==1&&(*(p+1)=='2'||*(p+1)=='3')){
	char flag = m_url[1];//0或1或2等
	//申请内存m_url_real，用于存储
	char *m_url_real = (char*)malloc(sizeof(char)*200);
	strcpy(m_url_real,"/");
	strcat(m_url_real,m_url+2);
	strncpy(m_real_file+len,m_url_real,FILENAME_LEN-len-1);
	free(m_url_real);
	//提取出用户名和密码
	char name[100],password[100];
	int i = 0;
	for(i = 5;m_string[i]!='&';i++){
	    name[i-5] = m_string[i];
	}
	//字符串末尾添0
	name[i-5] = '\0';
	int j =0;
	for(i = i+10;m_string[i]!='\0';++i,++j){
	    password[j] = m_string[i];
	} 
	//字符串末尾添0
	password[j] = '\0';

	if(*(p+1)=='3'){
	    //应该先检测数据库中是否有重名的。
	    //没有重名的话，就要进行数据增加。
	    char *sql_insert = (char*)malloc(sizeof(char)*200);
	    //这里应该仅仅是根据name和密码添加到数据库。一堆乱七八糟的玩意，本质上是写一个字符串。这可以封装成一个函数。另外操作字符数组也太麻烦了，还不如用string。
	    //有两个改进点。第一个就是可以用string避免这些字符指针操作；第二个就是可以把这些业务单独封装成一个方法
	    strcpy(sql_insert,"INSERT INTO user(username,passwd)VALUES()");
	    strcat(sql_insert,"'");
	    strcat(sql_insert,name);
	    strcat(sql_insert,"','");
	    strcat(sql_insert,password);
	    strcat(sql_insert,"')");
	    //如果注册的用户没有，那么就先添加到缓存，再放到数据库
	    if(users.find(name)==users.end()){
		//为什么这个位置要加互斥锁啊？我不理解orz
		m_lock.lock();
		printf("1\n");
		int res = mysql_query(mysql,sql_insert);
		printf("2\n");
		users.insert(pair<string,string>(name,name));
		printf("3\n");
		m_lock.unlock();
		printf("4\n");
		//如果数据库添加成功，那么就返回一个登陆界面。
		if(!res)
		    strcpy(m_url,"/log.html");
		//否则，就返回注册错误
		else
		    strcpy(m_url,"/registerError.html");
	    }
	    else strcpy(m_url,"/registerError.html");
	}
	//如果是登录
	else if(*(p+1)=='2'){
	    if(users.find(name)!=users.end()&&users[name]==password)
		strcpy(m_url,"/welcome.html");
	    else strcpy(m_url,"/logError.html");
	}
    }
    if(*(p+1)=='0'){
	//用cpp写个业务真吉尔麻烦，还要自己在堆上申请内存。我用java直接string就行了，管你啥内存，JVM自己都处理了。
	char *m_url_real = (char *)malloc(sizeof(char)*200);
	strcpy(m_url_real,"/register.html");
	strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
	free(m_url_real);
    }
    else if(*(p+1)=='1'){
        char *m_url_real = (char *)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/log.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p+1)=='5'){
        char *m_url_real = (char *)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/picture.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p+1)=='6'){
        //用cpp写个业务真吉尔麻烦，还要自己在堆上申请内存。我用java直接string就>行了，管你啥内存，JVM自己都处理了。
        char *m_url_real = (char *)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/video.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p+1)=='7'){
        //用cpp写个业务真吉尔麻烦，还要自己在堆上申请内存。我用java直接string就>行了，管你啥内存，JVM自己都处理了。
        char *m_url_real = (char *)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/fans.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }



    //其他情况，说明是get请求，url里面就有请求的网页地址比如index.html这样的东西，所以m_real_file只需要加上这个url就好。
    //get：直接从url读  post：消息体有字段：根据字段处理、然后return相应的界面    消息体无字段：根据字段比如1直接return相应的界面比如register.html
    //在doc_root后面跟着复制m_url
    else
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

//写HTTP响应(这个方法是主线程调用的，当工作线程已经处理完接受的请求，并且把相应数据放到了内存里。这个方法只是主线程调用来把内存的数据写入socket。)
bool http_conn::write(){
    int temp = 0;
    //int bytes_have_send = 0;//已经发送了多少字节。
    //int bytes_to_send = m_write_idx;//还剩多少字节没发送。
    //如果待发送数据为0，那么说明这次发送over，重新初始化。
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
	//如果已经发送的数据大于内存中第一块地方的长度，那么就要修改m_iv的具体值。因为writev系统调用不会自动帮你干
	if(bytes_have_send>=m_iv[0].iov_len){
	    m_iv[0].iov_len = 0;
	    m_iv[1].iov_base = m_file_address+(bytes_have_send-m_write_idx);
	    m_iv[1].iov_len = bytes_to_send;
	}
	else{
	    m_iv[0].iov_base = m_write_buf+bytes_have_send;
	    m_iv[0].iov_len = m_iv[0].iov_len-bytes_have_send;
	}
	//如果剩余未发送的为0，那么说明发完了，就可以重新注册该socket的可读事件。
	if(bytes_to_send<=0){
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

//往写缓冲write_buf中写入待发送的数据(是http响应的状态行和头部等)
//write_idx代表已经在这个buf中写了多少内容了。
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
		//这里就是m_iv真正被初始化的地方
		add_headers(m_file_stat.st_size);
		m_iv[0].iov_base = m_write_buf;
		m_iv[0].iov_len = m_write_idx;
		m_iv[1].iov_base = m_file_address;
		m_iv[1].iov_len = m_file_stat.st_size;
		m_iv_count = 2;
		//bytes_to_send存储了当前HTTP请求的响应部分的总待发送部分长度。m_write_idx表示栈上的writebuf大小，存储了HTTP响应的头部；m_file_stat.size表示了堆上的m_file_address大小，存储了HTTP响应的主体部分。
		bytes_to_send = m_write_idx+m_file_stat.st_size;
		return true;
	    }
	    else{
		const char* ok_string = "<html><body></body></html>";
		add_headers(strlen(ok_string));
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
