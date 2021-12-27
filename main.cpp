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

#include "locker/locker.h"
#include "threadpool/threadpool.h"
#include "http/http_conn.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd(int epollfd,int fd,bool one_shot,bool et);
extern int removefd(int epollfd,int fd);

//用于信号通知主线程epoll的管道。
static int pipefd[2];

//用于存储连接信息的定时器容器
//static tw_timer timer;


void sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1],(char*)&msg,1,0);
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

void show_error(int connfd,const char* info){
    printf("%s",info);
    send(connfd,info,strlen(info),0);
    close(connfd);
}

int main(int argc,char* argv[]){
    if(argc<=2){
	printf("usage:%s ip_address,port_number\n",basename(argv[0]));
	return -1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    //信号的处理(为特定信号设置信号处理函数)
    addsig(SIGALRM,sig_handler);
    addsig(SIGPIPE,SIG_IGN);

    //初始化线程池，此时所有线程都是饥饿状态，wait()死等着。
    threadpool<http_conn>* pool =NULL;
    try{
	pool = new threadpool<http_conn>;
    }
    catch(...)
    {
	return 1;
    }
    //为每个socket创建一个http_conn对象
    http_conn* users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    //监听socket的建立绑定监听等
    int listenfd = socket(AF_INET,SOCK_STREAM,0);
    assert(listenfd!=-1);

    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET,ip,&address.sin_addr);
    address.sin_port = htons(port);

    int ret = bind(listenfd,(sockaddr*)&address,sizeof(address));
    assert(ret!=-1);
 
    ret = listen(listenfd,5);
    assert(ret!=-1);

    //设置epoll并addfd   
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd!=-1);
    //listenfd并不需要设置oneshot，因为其只会被主线程处理，不存在并发问题。et设不设置无所谓。
    addfd(epollfd,listenfd,false,true);

    while(true){
	int number = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
	if((number<0)&&(errno!=EINTR)){
	    printf("epoll failure\n");
	    break;
	}

	for(int i = 0;i<number;i++){
	    int sockfd = events[i].data.fd;
	    if(sockfd == listenfd){
		struct sockaddr_in client_address;
		socklen_t client_addrlength = sizeof(client_address);
		int connfd = accept(listenfd,(struct sockaddr*)&client_address,&client_addrlength);
		if(connfd<0){
		    printf("errno is: %d\n",errno);
		}
		//m_user_count是http_conn的所有对象共享的一个静态变量，用以表示连接的用户数量。
		if(http_conn::m_user_count>=MAX_FD){
		    show_error(connfd,"Internal server busy");
		    continue;
		}
		//调用init函数为这个连接初始化一些东西，包括不限于读写buffer、文件名称buffer、客户端sock的fd与ip和端口地址。同时还要把这个connfd添加到epoll的内核表(同时注册EPOLLIN可读事件)。
		users[connfd].init(connfd,client_address);
	    }
	    //如果遇到异常，直接关闭该连接socket。
	    else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
		users[sockfd].close_conn();
	    }
	    //如果是监听到可读事件，那么说明连接socket有数据可读。先调用read()方法把数据读到一个http_conn对象的buffer里。
	    //从这里可以看出，这是一种reactor模式，主线程自己把数据的IO做好了，其他线程只需要处理逻辑业务。
	    else if(events[i].events & EPOLLIN){
		//如果读取成功，再调用子线程处理逻辑业务(就是分析解析http请求之类的)
		if(users[sockfd].read()){
		    pool->append(users+sockfd);
		}
		else{
		    users[sockfd].close_conn();
		}
	    }
	    //如果监听到可写事件，说明这个socket已经被处理完了，数据已经放在写buffer里，等待主线程把这个发送到socket里面。
	    else if(events[i].events & EPOLLOUT){
		//根据http的长短连接决定写完后是否要close这个连接。
		if(!users[sockfd].write()){
		    users[sockfd].close_conn();
		}
	    }
	    else{}

	}
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}
