#include "Socket.h"
#include "ipAddress.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
Socket::Socket(){
    sockFd = socket(AF_INET,SOCK_STREAM,0);
    //-1处理，todo

}
//bind绑定一个fd和地址。
void Socket::bind(ipAddress* addr){
    int ret = ::bind(sockFd,(sockaddr*)&addr->address,addr->addrLen);

}
void Socket::listen(){
    int ret = ::listen(sockFd,MAXLISTEN);
    //ret的处理，to do
    }
int Socket::accept(ipAddress* addr){
        int connFd =  ::accept(sockFd,(sockaddr*)&addr->address,&addr->addrLen);
        //ret的处理，to do
        return connFd;
    }
    //这个函数我觉得声明为全局函数比较好，因为不光socket用到了
void Socket::setnonblock(){
        fcntl(sockFd, F_SETFL, fcntl(sockFd, F_GETFL) | O_NONBLOCK);
    }
int Socket::getFd(){
    return sockFd;
}
Socket::~Socket(){
    if(sockFd!=-1)
    close(sockFd);
}