#include "ipAddress.h"
#include <string.h>

ipAddress::ipAddress():addrLen(sizeof(address)){
    bzero(&address,sizeof(address));

}
ipAddress::ipAddress(const char* ip,int port){
    //把address清0.为什么要清0？因为address是在栈上的变量，初始化不为0，需要重新清0.
    bzero(&address,sizeof(address));
    address.sin_family = AF_INET;
    //inet_pton把点分十进制字符串转换为网络字节序的ip地址。
    inet_pton(AF_INET,ip,&address.sin_addr);
    //htons把主机字节序变为网络字节序。这里的port是主机字节序，但是tcp传输时需要使用网络字节序。
    address.sin_port = htons(port);
    addrLen = sizeof(address);
}
// sockaddr_in ipAddress::getAddress(){
//     return address;
// }
// socklen_t ipAddress::getAddrLen(){
//     return addrLen;
// }
ipAddress::~ipAddress(/* args */)
{
}