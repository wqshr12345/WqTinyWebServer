#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<assert.h>
#include<sys/epoll.h>
#include "utils/Epoll.h"
#include "utils/ipAddress.h"
#include "utils/Socket.h"
#include "locker/locker.h"
#include "threadpool/threadpool.h"
#include "http/http_conn.h"
#include "timer/lst_timer.h"
#include "log/log.h"
#include "CGImysql/sql_connection_pool.h"


#define MAX_FD 65536  //最大文件描述符
//#define MAX_EVENT_NUMBER 10000  //epoll最大监听事件数
#define TIMESLOT 5  
//最小超时单位

//#define SYNLOG  //同步写日志
#define ASYNLOG //异步写日志

//在另一个cpp文件中定义的三个函数
extern int addfd(int epollfd,int fd,bool one_shot);
extern int removefd(int epollfd,int fd);
extern int setnonblocking(int fd);

//用于信号通知主线程epoll的管道。
static int pipefd[2];

//用于存储连接信息的定时器容器
static sort_timer_lst timer_lst;

static int epollfd;

//一.信号处理部分。主要是注册信号处理函数等相关函数。
void sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1],(char*)&msg,1,0);
    errno = save_errno;
}


void addsig(int sig,void(handler)(int),bool restart = true){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    if(restart){
	//这表示进程重新调用被该信号终止的系统调用
	sa.sa_flags != SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=-1);
}

//alarm信号到点时该干的事
void timer_handler(){
    timer_lst.tick();
    alarm(TIMESLOT);
}

//定时器回调函数。当定时器容器内的某个定时器到点了，就自动调用这个函数，来删除该socket的注册事件。
void cb_func(client_data *user_data){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,user_data->sockfd,0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}

void show_error(int connfd,const char* info){
    printf("%s",info);
    send(connfd,info,strlen(info),0);
    close(connfd);
}

int main(int argc,char* argv[]){
#ifdef ASYNLOG
    Log::get_instance()->init("ServerLog",2000,800000,8);//异步日志模型
#endif
#ifdef SYNLOG
    Log::get_instance()->init("ServerLog",2000,800000,0);//同步日志模型
#endif
    if(argc<=2){
	printf("usage:%s ip_address,port_number\n",basename(argv[0]));
	return -1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    //初始化数据库连接池
    connection_pool *connPool = connection_pool::Instance();
    connPool->init("localhost","root","wqshr0425","web",3306,8);    
    
    //初始化线程池，此时所有线程都是饥饿状态，wait()死等着。
    threadpool<http_conn>* pool =NULL;
    try{
	pool = new threadpool<http_conn>(connPool);
    }
    catch(...)
    {
	return 1;
    }
    
    //为每个socket创建一个http_conn对象
    http_conn* users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    //初始化数据库读取表
    users->initmysql_result(connPool);
    
    //监听socket的建立绑定监听等
	Socket* listenSocket = new Socket();
	listenSocket->setnonblock();
    //int listenfd = socket(AF_INET,SOCK_STREAM,0);
    //assert(listenfd!=-1);

	ipAddress* adddress = new ipAddress(ip,port);
    //struct sockaddr_in address;
    //bzero(&address,sizeof(address));
    //address.sin_family = AF_INET;
    //inet_pton(AF_INET,ip,&address.sin_addr);
    //address.sin_port = htons(port);

	listenSocket->bind(adddress);
    // int ret = bind(listenfd,(sockaddr*)&address,sizeof(address));
    // assert(ret!=-1);
	listenSocket->listen();
    // ret = listen(listenfd,5);
    // assert(ret!=-1);

	Epoll* epoll = new Epoll();
    //设置epoll并addfd   
    // epoll_event events[MAX_EVENT_NUMBER];
    // epollfd = epoll_create(5);
    // assert(epollfd!=-1);
	printf("%d listenfd",listenSocket->getFd());
	epoll->addFd(listenSocket->getFd());
    //listenfd并不需要设置oneshot，因为其只会被主线程处理，不存在并发问题。et设不设置无所谓。
    //addfd(epollfd,listenfd,false);

    //！！！之前没加这一行，所以http_conn类里的epollfd默认是0，那init的时候都注册到0里面了，注册了个寂寞！
    http_conn::m_epollfd = epoll->getFd();    
    
    //创建管道，用于监听信号。实现统一事件处理
    int ret = socketpair(PF_UNIX,SOCK_STREAM,0,pipefd);
    assert(ret!=-1);
    setnonblocking(pipefd[1]);
    epoll->addFd(pipefd[0]);
	//addfd(epollfd,pipefd[0],false);

    //信号的处理(为特定信号设置信号处理函数)
    addsig(SIGALRM,sig_handler);
    addsig(SIGPIPE,SIG_IGN);
    bool stop_server = false;

    //设置client_data数组，用于存放具体fd和定时器的对应关系。
    client_data *users_timer = new client_data[MAX_FD];

    //初始添加alarm信号信息
    bool timeout = false;
    alarm(TIMESLOT);


    while(!stop_server){
		printf("debug1\n");
		pair<epoll_event*,int> epollPair = epoll->epoll_wait(-1);
		printf("debug2\n");
	printf("%d\n",epollPair.second);
	//int number = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
	if((epollPair.second <0)&&(errno!=EINTR)){
	    LOG_ERROR("%s","epoll failure");
	    //printf("epoll failure\n");
	    break;
	}
	for(int i = 0;i<epollPair.second;i++){
	    int sockfd = epollPair.first[i].data.fd;
	    if(sockfd == listenSocket->getFd()){
		printf("新连接到咯!\n");
		ipAddress* client_address = new ipAddress();
		//struct sockaddr_in client_address;
		//socklen_t client_addrlength = sizeof(client_address);
		
		int connfd = listenSocket->accept(client_address);
		//int connfd = accept(listenfd,(struct sockaddr*)&client_address,&client_addrlength);
		printf("这次的sockfd是%d\n",connfd);
		if(connfd<0){
		    //printf("errno is: %d\n",errno);
		    LOG_ERROR("%serrno is:%d","accept error",errno);
		    continue;
		}
		//m_user_count是http_conn的所有对象共享的一个静态变量，用以表示连接的用户数量。
		if(http_conn::m_user_count>=MAX_FD){
		    show_error(connfd,"Internal server busy");
		    LOG_ERROR("%S","Internal server busy");
		    continue;
		}
		//调用init函数为这个连接初始化一些东西，包括不限于读写buffer、文件名称buffer、客户端sock的fd与ip和端口地址。同时还要把这个connfd添加到epoll的内核表(同时注册EPOLLIN可读事件)。
		users[connfd].init(connfd,client_address->address);

		//初始化client_data数据  讲道理，这个client_data类的内容可以定义在http_conn类里面，无非就在里面多加一个定时器罢了。
		//而且我觉得下面的可以封装成一个client_data中的方法。
		users_timer[connfd].address = client_address->address;
		users_timer[connfd].sockfd = connfd;
		util_timer *timer = new util_timer;
		timer->user_data = &users_timer[connfd];
		timer->cb_func = cb_func;
		time_t cur = time(NULL);
		timer->expire = cur+3*TIMESLOT;
		users_timer[connfd].timer = timer;
		timer_lst.add_timer(timer);


	    }
	    //如果遇到异常，直接关闭该连接socket。
	    else if(epollPair.first[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
		//直接调用定时器的方法去close这个连接，同时把这个定时器从链表里删掉
		util_timer *timer = users_timer[sockfd].timer;
		timer->cb_func(&users_timer[sockfd]);
		if(timer){
		    timer_lst.del_timer(timer);
		}
		//users[sockfd].close_conn();
	    }
	    //处理信号
	    else if((sockfd == pipefd[0])&&(epollPair.first[i].events & EPOLLIN)){
		int sig;
		char signals[1024];
		ret = recv(pipefd[0],signals,sizeof(signals),0);
		if(ret == -1)
		    continue;
		else if (ret == 0)
		    continue;
		else{
		    for(int i = 0;i<ret;i++){
			switch(signals[i]){
			    case SIGALRM:{
				//因为定时任务不急着处理，在这次epoll之后再处理
				printf("timeout!\n");
				timeout = true;
				break;
			    }
			    case SIGTERM:{
				stop_server = true;
			    }
			}
		    }
		}
	    }
	    //如果是监听到可读事件，那么说明连接socket有数据可读。先调用read()方法把数据读到一个http_conn对象的buffer里。
	    //从这里可以看出，这是一种reactor模式，主线程自己把数据的IO做好了，其他线程只需要处理逻辑业务。
	    else if(epollPair.first[i].events & EPOLLIN){
		//如果读取成功，再调用子线程处理逻辑业务(就是分析解析http请求之类的)
		util_timer *timer = users_timer[sockfd].timer;
		if(users[sockfd].read()){
		    LOG_INFO("deal with the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
		    Log::get_instance()->flush();
		    pool->append(users+sockfd);
		    //因为有读事件，所以要让定时器的到期时间后延
		    if(timer){
			time_t cur = time(NULL);
			timer->expire = cur+3*TIMESLOT;
			timer_lst.adjust_timer(timer);
			LOG_INFO("%s","adjust timer once");
			Log::get_instance()->flush();
		    }
		}
		//如果读取失败，就应该close此fd。原来是用close_conn方法，现在也可以用定时器里的cb_func
		else{
		    timer->cb_func(&users_timer[sockfd]);
		    if(timer){
			timer_lst.del_timer(timer);
		    }
		    //users[sockfd].close_conn();
		    
		}
	    }
	    //如果监听到可写事件，说明这个socket已经被处理完了，数据已经放在写buffer里，等待主线程把这个发送到socket里面。
	    else if(epollPair.first[i].events & EPOLLOUT){
		util_timer *timer = users_timer[sockfd].timer;
		//根据http的长短连接决定写完后是否要close这个连接。
		if(!users[sockfd].write()){
		    //如果是短连接，就断掉
		    printf("这次是短连接\n");
		    timer->cb_func(&users_timer[sockfd]);
		    if(timer){
			timer_lst.del_timer(timer);
		    } 
		   // users[sockfd].close_conn();
		}
		//如果是长连接，定时器才有用...
		else{
		    LOG_INFO("send data to the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
		    Log::get_instance()->flush();
		    if(timer){
			time_t cur = time(NULL);
			timer->expire = cur+3*TIMESLOT;
			timer_lst.adjust_timer(timer);
		    }
		}
	    }
	    else{}

	}
	if(timeout){
	    timer_handler();
	    timeout = false;
	}
    }
    //close(epollfd);
    //close(listenfd);
	delete epoll;
	delete listenSocket;
	delete adddress;
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users_timer;
    delete [] users;
    delete pool;
    return 0;
}
