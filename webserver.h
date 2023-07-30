#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件监听数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    // 初始化
    void init(int port, string user, string passWord, string databaseName,
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();                                        // 线程池
    void sql_pool();                                           // 数据库连接池
    void log_write();                                          // 日志
    void trig_mode();                                          // 触发模式
    void eventListen();                                        // 事件监听
    void eventLoop();                                          // 运行
    void timer(int connfd, struct sockaddr_in client_address); // 定时器
    void adjust_timer(util_timer *timer);                      // 定时器时间调整
    void deal_timer(util_timer *timer, int sockfd);            // 删除定时器
    bool dealclinetdata();                                     // 处理客户端连接
    bool dealwithsignal(bool &timeout, bool &stop_server);     // 处理信号
    void dealwithread(int sockfd);                             // 处理读事件
    void dealwithwrite(int sockfd);                            // 处理写事件

public:
    // 基础
    int m_port; // 端口号
    char *m_root;
    int m_log_write;  // 日志写入方式
    int m_close_log;  // 标记是否关闭日志功能
    int m_actormodel; // 并发模型选择类型

    int m_pipefd[2];  // socketpair函数第四个参数，套节字柄对，进行双向读写操作
    int m_epollfd;    // epoll文件描述符
    http_conn *users; // http_conn类指针 保存所有客户端信息

    // 数据库相关
    connection_pool *m_connPool; // 创建的数据库连接池
    string m_user;               // 登陆数据库用户名
    string m_passWord;           // 登陆数据库密码
    string m_databaseName;       // 使用的数据库名
    int m_sql_num;               // 数据库连接池数量

    // 线程池相关
    threadpool<http_conn> *m_pool; // 创建的线程池
    int m_thread_num;              // 线程池内的线程数量

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER]; // epoll事件数组

    int m_listenfd;   // socket文件描述符
    int m_OPT_LINGER; // 是否优雅关闭链接
    int m_TRIGMode;   // Epoll对文件操作符的操作的触发模式
    // 事件触发模式
    int m_LISTENTrigmode; // 监听触发模式
    int m_CONNTrigmode;   // 连接触发模式

    // 定时器相关
    client_data *users_timer;
    Utils utils;
};
#endif
