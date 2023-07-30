#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

// 初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url; //"localhost"
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log; // 日志开关

	// 根据最大链接数参数创建连接池
	for (int i = 0; i < MaxConn; i++)
	{
		/*
		mysql_init()函数用来分配或者初始化一个MYSQL对象，用于连接mysql服务端
		如果传入的参数是NULL指针，它将自动为你分配一个MYSQL对象
		如果这个MYSQL对象是它自动分配的，那么在调用mysql_close的时候，会释放这个对象。
		*/

		MYSQL *con = NULL;
		con = mysql_init(con);
		/*
		exit(int status)
		status 是一个整型参数，可以利用这个参数传递 进程结束时的状态
		一般来说，exit(0) 表示程序正常退出，exit(1) 或者 exit(-1) 表示程序异常退出。
		*/
		if (con == NULL)
		{
			LOG_ERROR("MySQL-mysql_init() Error"); // 写入日志
			exit(1);				  // exit()函数用来终止进程
		}
		/*
		MYSQL *mysql_real_connect (MYSQL *mysql,
								  const char *host,
								  const char *user,
								  const char *passwd,
								  const char *db,
								  unsigned int port,
								  const char *unix_socket,      unix_socket为null时，表明不使用socket或管道机制，
								  unsigned long client_flag)    最后一个参数经常设置为0
		mysql_real_connect()尝试与运行在主机上的MySQL数据库引擎建立连接。
		在你能够执行需要有效MySQL连接句柄结构的任何其他API函数之前，mysql_real_connect()必须成功完成。
		*/
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL-mysql_real_connect() Error");
			exit(1);
		}
		connList.push_back(con);
		++m_FreeConn;
	}

	reserve = sem(m_FreeConn); // 将空闲链接池数据传入信号

	m_MaxConn = m_FreeConn;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	reserve.wait(); // 连接池连接数量-1

	lock.lock();

	con = connList.front();
	connList.pop_front();

	--m_FreeConn;
	++m_CurConn;

	lock.unlock();
	return con;
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	reserve.post(); //连接池连接信号+1
	return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

// 当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

// 析构
connection_pool::~connection_pool()
{
	DestroyPool();
}

/*
在获取连接时，通过有参构造对传入的参数进行修改。
其中数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改。
*/
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	*SQL = connPool->GetConnection();

	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
	poolRAII->ReleaseConnection(conRAII);
}
