#ifndef SQL_CONNECTION_POOL
#define SQL_CONNECTION_POOL

#include<stdio.h>
#include<list>
#include<mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include<string>
#include<../locker/locker.h>

using namespace std;

class connection_pool{
    public:
	MYSQL *GetConnection();//获取数据库连接
	bool ReleaseConnection(MYSQL *conn);//释放数据库连接
	int GetFreeConn();//获取连接？
	void DEstroyPool();//销毁所有连接

	//单例模式
	static connection_pool *Instance();
	
	//初始化方法
	void init(string url,string User,string PassWord,string DataBaseName,int Port,unsigned int MaxConn);
	connection_pool();
	~connection_pool();

    private:
	unsigned int MaxConn;//最大连接数
	unsigned int CurConn;//当前已使用的连接数
	unsigned int FreeConn;//当前空闲的连接数

    private:
	locker lock;
	list<MYSQL *> connList;//连接池
	sem reserve;

    private:
	string Url;//主机地址
	string Port;//数据库端口
	string User;//登录数据库用户名
	string PassWord;//登录数据库密码
	string DatbaseName;//数据库名
};

class connectionRALL{
    public:
	connectionRALL(MYSQL **con,connection_pool *connPool);
	~connectionRALL();

    private:
	MYSQL *conRALL;
	connection_pool *poolRALL;
};

#endif
