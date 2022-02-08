#include<arpa/inet.h>
class ipAddress{
public:
    struct sockaddr_in address;
    socklen_t addrLen;
public:
    ipAddress();
    ipAddress(const char* ip,int port);
    // sockaddr_in getAddress();
    // socklen_t getAddrLen();
    ~ipAddress();
};


