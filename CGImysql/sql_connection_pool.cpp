#include<mysql/mysql.h>
#include<stdio.h>
#include<string>
#include<string.h>
#include<stdlib.h>
#include<list>
#include<pthread.h>
#include<iostream>
#include "sql_connection_pool.h"

using namespace std;

//数据库连接池构造函数
connection_pool::connection_pool(){
    this->CurConn = 0;
    this->FreeConn = 0;
}

//单例模式
connection_pool *connection_pool::Instance(){
    static connection_pool connPool;
    return &connPool;
}

//初始化数据库连接池方法
void connection_pool::init(string Url,strign User,string PassWord,strign DBName,int Port,unsigned int MaxConn){
    this->Url = Url;
    this->Port = Port;
    this->User = User;
    this->PassWord = PassWord;
    this->DatabaseName = DBName;
    lock.lock();
    //建立连接池
    for(int i = 0;i<MaxConn;i++){
	MYSQL *con = NULL;
	con = mysql_init(con);
	if(con == NULL){
	    cout<<"Error:"<<mysql_error(con);
	    exit(1);
	}
	con = mysql_real_connect(con,Url.c_str(),User.c_str(),PassWord.c_str(),DBName.c_str(),Port,NULL,0);
	if(con == NULL){
	    cout<<"Error:"<<mysql_error(con);
	    exit(1);
	}
	//将该数据库连接加入连接池
	connList.push_back(con);
	++FreeConn;
    }
    reserve = sem(FreeConn);
    this->MaxConn = FreeConn;
    lock.unlock();
}

//从连接池返回一个可用连接，同时更新使用和空闲连接数。类似消费者
MYSQL *connection_pool::GetConnection(){
    MYSQL *con = NULL;
    //讲道理，下面有reserve的wait，这里判断这个有何意义？尝试删掉..
    if(0==connList.size()){
	return NULL;
    }
    //这里其实就类似生产者消费者过程。Getonnection方法可能被好多线程同时调用，而这个方法又涉及对Connlist的修改，所以需要同步，使用一个信号量+一个互斥锁。
    reserve.wait();
    lock.lock();
    
    con = connList.front();
    connList.pop_front();

    --FreeConn;
    ++CurConn;
    lock.unlock();
    return con;
}

//释放一个连接到连接池。类似生产者(只不过这个生产者永远不会阻塞，因为数量最多就是Max)
bool connection_pool::REleaseConnection(MYSQL *con){
    if(NULL == con)
	return false;
    lock.lock();
    connList.push_back(con);
    ++FreeConn;
    --CurConn;
    lock.unlock();
    reserve.post();
    return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool(){
    lock.lock();
    if(connList.size()>0){
	list<MYSQL *>::iterator it;
	for(it = connList.begin();it!=connList.end();++it){
	    MYSQL *con = *it;
	    mysql_close(con);
	}
	CurConn = 0;
	FreeConn = 0;
	connlist.clear();
	lock.unlock();
    }
    lock.unlock();
}

int connection_pool::GetFreeConn(){
    return this->FreeConn;
}

connection_pool::~connection_pool(){
    DestroyPool();
}


//RALL机制释放资源。
connectionRALL::connectionRALL(MYSQL **SQL,connection_pool *connPool){
    *SQL = connPool->GetConnection();
    conRALL = *SQL;
    poolRALL = connPool;
}

connectionRALL::~connectionRALL(){
    pollRALL->ReleaseConnection(conRALL);
}
