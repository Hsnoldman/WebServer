#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;  // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; // 写缓冲区大小
    // HTTP请求方法
    enum METHOD
    {
        GET = 0, // 向特定的资源发出请求
        POST,    // 向指定资源提交数据进行处理请求
        HEAD,    // 向服务器索要与GET请求相一致的响应，只不过响应体将不会被返回
        PUT,     // 向指定资源位置上传其最新内容
        DELETE,  // 请求服务器删除Request-URL所标识的资源
        TRACE,   // 回显服务器收到的请求，主要用于测试或诊断
        OPTIONS, // 返回服务器针对特定资源所支持的HTTP请求方法
        CONNECT, // HTTP/1.1协议中预留给能够将连接改为管道方式的代理服务器
        PATH
    };
    // 服务器处理HTTP请求可能的结果，报文解析结果
    enum HTTP_CODE
    {
        NO_REQUEST,        // 请求不完整，需要继续读取客户数据
        GET_REQUEST,       // 表示获得了一个完整的客户请求
        BAD_REQUEST,       // 表示客户请求语法错误
        NO_RESOURCE,       // 表示服务器没有资源
        FORBIDDEN_REQUEST, // 表示客户对资源没有足够的访问权限
        FILE_REQUEST,      // 文件请求，获取文件成功
        INTERNAL_ERROR,    // 表示服务器内部错误
        CLOSED_CONNECTION  // 表示客户端已经关闭连接了
    };
    // 解析客户端请求时，主状态机的状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, // 当前正在分析请求行
        CHECK_STATE_HEADER,          // 当前正在分析请求头
        CHECK_STATE_CONTENT          // 当前正在分析请求体
    };
    // 从状态机的状态
    enum LINE_STATUS
    {
        LINE_OK = 0, // 读取到一个完整的行
        LINE_BAD,    // 行出错
        LINE_OPEN    // 行数据尚且不完整
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname); // 初始化连接
    void close_conn(bool real_close = true);                                                                      // 关闭连接
    void process();                                                                                               // 处理客户端请求
    bool read_once();                                                                                             // 非阻塞读
    bool write();                                                                                                 // 非阻塞写
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool); // 初始化数据库连接
    int timer_flag;
    int improv;

private:
    void init();              // 初始化
    HTTP_CODE process_read(); // 读数据
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);               // 解析请求首行
    HTTP_CODE parse_headers(char *text);                    // 解析请求头
    HTTP_CODE parse_content(char *text);                    // 解析请求体
    HTTP_CODE do_request();                                 // 处理请求
    char *get_line() { return m_read_buf + m_start_line; }; // 内联函数，获取一行数据
    LINE_STATUS parse_line();                               // 获取一行数据，交给主状态机处理
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;    // epoll文件描述符，设置为static，全局可见，所有的socket上的事件都被注册到同一个epoll对象中
    static int m_user_count; // 统计用户数量
    MYSQL *mysql;
    int m_state; // 读为0, 写为1

private:
    int m_sockfd;                        // 该http连接的socket
    sockaddr_in m_address;               // 通信的socket地址
    char m_read_buf[READ_BUFFER_SIZE];   // 读缓冲区
    long m_read_idx;                     // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    long m_checked_idx;                  // 当前正在分析的字符在读缓冲区的位置
    int m_start_line;                    // 当前正在解析的行的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE]; // 写缓冲区
    int m_write_idx;                     // 写缓冲区待发送字节数
    CHECK_STATE m_check_state;           // 主状态机的当前状态
    char m_real_file[FILENAME_LEN];      // 存储完整的资源路径
    METHOD m_method;                     // HTTP请求方法
    char *m_url;                         // 请求目标文件的文件名
    char *m_version;                     // HTTP协议版本
    char *m_host;                        // 主机名：ip地址及端口号
    long m_content_length;               // 请求体长度
    bool m_linger;                       // 判断是否保持连接keep alive，长连接或短链接
    char *m_file_address;                // 读取服务器上的文件地址
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;             // 是否启用的POST
    char *m_string;      // 存储请求头数据
    int bytes_to_send;   // 剩余发送字节数
    int bytes_have_send; // 已发送字节数
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
