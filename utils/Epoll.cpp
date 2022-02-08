#include "Epoll.h"
//#include "util.h"
//#include <vector>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#define MAX_EVENTS 1000

//epoll的构造函数，默认生成一个epoll内核事件表。
Epoll::Epoll() : epollFd(-1), events(nullptr){
    epollFd = epoll_create(5);
    //针对错误的逻辑处理 to do
    events = new epoll_event[MAX_EVENTS];
   
    //这里的bzero有必要吗？
    bzero(events, sizeof(*events)*MAX_EVENTS);
}

//epoll的析构函数，默认关闭这个epoll内核事件表。
Epoll::~Epoll(){
    if(epollFd != -1){
        close(epollFd);
        epollFd = -1;
    }
    delete [] events;
}
//注册fd到epoll的监听列表里面,默认监听EPOLLIN事件和EPOLLET。为了同步，使用了EPOLLONESHOT
void Epoll::addFd(int fd){
    uint32_t ev=EPOLLIN|EPOLLRDHUP|EPOLLET|EPOLLONESHOT;
    struct epoll_event event;
    bzero(&event, sizeof(event));
    event.data.fd = fd;
    event.events = ev;
    printf("%d",epollFd);
    printf(" ");
    printf("%d",fd);
    int ret = epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event);
    if(ret == -1){
        printf("epoll add error\n");
    }
    //setnonblocking(fd);
}

//进行epoll_wait，并返回就绪事件数组。如果返回数组本身，数组必须是new生成的，这时可以返回epoll_wait的指针，同时返回一个数组大小，避免越界。
//为了避免麻烦，可以使用vector封装。
std::pair<epoll_event*,int> Epoll::epoll_wait(int timeout){
    //std::vector<epoll_event> activeEvents;
    int nfds = ::epoll_wait(epollFd, events, MAX_EVENTS,timeout);
    //errif(nfds == -1, "epoll wait error");
    // for(int i = 0; i < nfds; ++i){
    //     activeEvents.push_back(events[i]);
    // }
    return std::pair<epoll_event*,int>(events,nfds);
}
int Epoll::getFd(){
    return epollFd;
}