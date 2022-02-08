#define MAXLISTEN 5
//如果一个类的方法声明中想要使用另一个类，需要进行声明，不需要引入头文件
class ipAddress;
class Socket
{
private:
    int sockFd;
public:
    Socket();
    void bind(ipAddress*);
    void listen();
    int accept(ipAddress*);
    //这个函数我觉得声明为全局函数比较好，因为不光socket用到了
    void setnonblock();
    int getFd();
    ~Socket();
};


