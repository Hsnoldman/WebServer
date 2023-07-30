#include "webserver.h"

WebServer::WebServer()
{
    // http_conn类对象
    users = new http_conn[MAX_FD]; // 保存所有客户端信息的数组

    // root文件夹路径
    char server_path[200];
    // getcwd()会将当前工作目录的绝对路径复制到参数server_path所指的内存空间中,参数size为server_path的空间大小。
    getcwd(server_path, 200);
    char root[11] = "/resources";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root); // strcat字符串追加/连接函数

    // 用户定时器数组
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

// 初始化
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write,
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;
    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

/*
Epoll对文件操作符的操作的触发模式
    LT(level-triggered)：水平触发、电平触发、条件触发
        一个事件只要有，就会一直触发
    ET(edge-triggered)：边缘触发、边沿触发
        只有一个事件从无到有才会触发

1.LT模式会一直触发EPOLLOUT，当缓冲区有数据时会一直触发EPOLLIN
2.ET模式会在连接建立后触发一次EPOLLOUT，当收到数据时会触发一次EPOLLIN
3.LT模式触发EPOLLIN时可以按需读取数据，残留了数据还会再次通知读取
4.ET模式触发EPOLLIN时必须把数据读取完，否则即使来了新的数据也不会再次通知了
5.LT模式的EPOLLOUT会一直触发，所以发送完数据记得删除，否则会产生大量不必要的通知
6.ET模式的EPOLLOUT事件若数据未发送完需再次注册，否则不会再有发送的机会
  通常发送网络数据时不会依赖EPOLLOUT事件，只有在缓冲区满发送失败时会注册这个事件，期待被通知后再次发送

listenfd和connfd的模式组合
*/
void WebServer::trig_mode()
{
    // LT + LT
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // LT + ET
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // ET + LT
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // ET + ET
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

/*
初始化日志写入方式
同步：判断是否分文件
    直接格式化输出内容，将信息写入日志文件
异步：判断是否分文件
    格式化输出内容，将内容写入阻塞队列，创建一个写线程，从阻塞队列取出内容写入日志文件
*/
void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        // 初始化日志
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

// 初始化数据库连接池
void WebServer::sql_pool()
{
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    users->initmysql_result(m_connPool);
}

// 初始化线程池
void WebServer::thread_pool()
{
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

// 事件监听
/*
服务端首先初始化Socket() --> 和接口进行绑定bind()和监听listen() --> 调用accept()进行阻塞
*/
void WebServer::eventListen()
{
    // 网络编程基础步骤
    /*
    int socket(int domain, int type, int protocol)
        -domain: 协议族,决定了socket的地址类型,在通信中必须采用相应的地址.
        -type: 指定socket的类型.
        -protocol: 协议 当protocol为0时,会自动选择type类型对应的默认协议.
    */
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);

    // assert函数 作用：如果它的条件返回错误，则终止程序执行
    /*
    如果其值为假（即为0），那么它先向stderr打印一条出错信息，然后通过调用 abort 来终止程序运行。
    */
    assert(m_listenfd >= 0);

    /*
    通过linger结构体设置socket断开连接的方式
    struct linger
    {
        int l_onoff;
        int l_linger;
    };
        l_onoff>0时,在调用closesocket的时候不会立刻返回
                    内核会延迟一段时间，这个时间就由l_linger 得值来决定。
                    如果超时时间到达之前，发送完未发送的数据(包括FIN包)并得到另一端的确认，closesocket会返回正确，socket描述符优雅退出。
                    否则，closesocket会直接返回 错误值，未发送数据丢失，socket描述符被强制性退出。
    */

    // 优雅(强制)关闭连接  设置端口复用
    /*
    端口复用允许在一个应用程序可以把多个套接字绑在一个端口上而不出错
    端口复用最常用的用途是:
        1.防止服务器重启时之前绑定的端口还未释放
        2.程序突然退出而系统没有释放端口
    */
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};                                       // 在closesocket的时候立刻返回，底层会将未发送完的数据发送完成后再释放资源，优雅的退出。
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp)); // 端口复用
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    // 绑定
    int ret = 0;
    struct sockaddr_in address;                  // sockaddr_in结构体用来存储网络通信的地址
    bzero(&address, sizeof(address));            // 清零，初始化
    address.sin_family = AF_INET;                // 地址族
    address.sin_addr.s_addr = htonl(INADDR_ANY); // 32位IP地址，IPv4
    address.sin_port = htons(m_port);            // 16位TCP/UDP端口号

    int flag = 1;
    // setsockopt设置套接字描述符的属性
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)); //
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));  // 绑定
    assert(ret >= 0);
    /*
    listen函数的第一个参数时即将要监听的socket文件描述符，第二个参数为相应的socket可以排队的最大连接数。
    socket()创建的socket默认是一个主动类型，listen则将socket变成被动类型，等待客户连接请求。
    */
    ret = listen(m_listenfd, 5); // 监听
    assert(ret >= 0);

    utils.init(TIMESLOT); // timer相关

    // epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER]; // 事件监听数组
    m_epollfd = epoll_create(5);          // int epoll_create(int size); 创建一个epoll的句柄，size用来告诉内核这个监听的数目一共有多大。
    assert(m_epollfd != -1);

    // 将监听的文件描述符添加到epoll对象中
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd; // 将文件描述符同步到http_conn类中

    // 使用socketpair函数能够创建一对套节字进行进程间通信（IPC）
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd); // m_pipefd[0]和m_pipefd[1]为创建好的两个套接字
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    // alarm函数的作用是设置一个定时器，在TIMESLOT秒之后，将会发送SIGALRM信号给当前的进程
    // 如果不对SIGALRM信号进行忽略或者捕捉，默认情况下会退出进程
    alarm(TIMESLOT);

    // 工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

// 初始化一个用户连接的定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    // 初始化client_data数据
    // 创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

// 若有数据传输，则将定时器往后延迟3个单位
// 并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

// 删除定时器
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

// 处理客户端连接
bool WebServer::dealclinetdata()
{
    struct sockaddr_in client_address; // socket地址
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode) // LT水平触发
    {
        /*
        accept()接受一个客户端的连接请求，并返回一个新的套接字。
        所谓“新的”就是说这个套接字与socket()返回的用于监听和接受客户端的连接请求的套接字不是同一个套接字。
        与本次接受的客户端的通信是通过在这个新的套接字上发送和接收数据来完成的

        如果accept成功，那么其返回值是由内核自动生成的一个全新描述符，代表与客户端的TCP连接。
        一个服务器通常仅仅创建一个监听套接字，它在该服务器生命周期内一直存在。
        内核为每个由服务器进程接受的客户端连接创建一个已连接套接字。
        当服务器完成对某个给定的客户端的服务器时，相应的已连接套接字就被关闭。
        */
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD) // 目前的连接数满了
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }

        timer(connfd, client_address);
    }

    // ET边缘触发
    else
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

// 处理信号
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

// 处理读事件
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        // 若监测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    // proactor
    else
    {

        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            // 若监测到读事件，将该事件放入请求队列
            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

// 处理写事件
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    // reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else
    {
        // proactor
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

// 运行
void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        // number：检测到的事件个数
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        // 处理调用失败
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        // 循环遍历事件数组
        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            // 处理新到的客户连接
            if (sockfd == m_listenfd) // 有客户端连接进来
            {
                bool flag = dealclinetdata();
                if (false == flag)
                    continue;
            }
            // 对方异常断开或者错误等事件
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}