#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
	MYSQL *GetConnection();				 // 获取数据库连接
	bool ReleaseConnection(MYSQL *conn); // 释放连接
	int GetFreeConn();					 // 获取连接
	void DestroyPool();					 // 销毁所有连接

	// 单例模式
	static connection_pool *GetInstance();

	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

private:
	connection_pool();
	~connection_pool();

	int m_MaxConn;	// 最大连接数
	int m_CurConn;	// 当前已使用的连接数
	int m_FreeConn; // 当前空闲的连接数
	locker lock;
	list<MYSQL *> connList; // 连接池
	sem reserve; //数据库连接池数量信号量

public:
	string m_url;		   // 主机地址
	string m_Port;		   // 数据库端口号
	string m_User;		   // 登陆数据库用户名
	string m_PassWord;	   // 登陆数据库密码
	string m_DatabaseName; // 使用数据库名
	int m_close_log;	   // 日志开关
};

//RAII类
/*
RAII全称是“Resource Acquisition is Initialization”，直译过来是“资源获取即初始化”.

在构造函数中申请分配资源，在析构函数中释放资源。因为C++的语言机制保证了，当一个对象创建的时候，自动调用构造函数，当对象超出作用域的时候会自动调用析构函数。所以，在RAII的指导下，我们应该使用类来管理资源，将资源和对象的生命周期绑定

***RAII的核心思想是：将资源或者状态与对象的生命周期绑定

通过C++的语言机制，实现资源和状态的安全管理,智能指针是RAII最好的例子
*/

//将数据库连接的获取与释放通过RAII机制封装，避免手动释放。
class connectionRAII
{

public:
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();

private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif
