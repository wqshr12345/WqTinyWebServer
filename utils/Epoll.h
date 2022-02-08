#include<sys/epoll.h>
#include <utility>
#define MAX_EVENT_NUMBER 1000;
class Epoll{
private:
    //epoll事件表。
    int epollFd;
    //用于epoll_wait时作为返回数组。
    struct epoll_event* events;
public:
    Epoll(/* args */);
    void addFd(int fd);//uint32_t ev
    std::pair<epoll_event*,int> epoll_wait(int timeout);
    int getFd();
    ~Epoll();
};


