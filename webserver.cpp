#include "webserver.h"

WebServer::WebServer() 
    : m_epoller(MAX_EVENT_NUMBER)
{
    m_conn_mgr = new ConnectionManager(MAX_FD, 3 * TIMESLOT);
    m_acceptor = NULL;

    // root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

}

WebServer::~WebServer()
{
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete m_acceptor;
    delete m_conn_mgr;
    delete m_pool;
}

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

void WebServer::sql_pool()
{
    // 初始化数据库连接池
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化数据库读取表
    m_conn_mgr->get(0)->initmysql_result(m_connPool);
}

void WebServer::thread_pool()
{
    // 线程池
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::eventListen()
{
    // 网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    // 优雅关闭连接
    if (0 == m_OPT_LINGER)
    {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER)
    {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(m_listenfd, 512);
    assert(ret >= 0);

    // listenfd 已就绪，创建 Acceptor
    m_acceptor = new Acceptor(m_listenfd, m_LISTENTrigmode);
    m_acceptor->setCloseLog(m_close_log);

    utils.setnonblocking(m_listenfd);
    m_epoller.add(m_listenfd,
                  Epoller::makeEvents(/*want_read=*/true, /*one_shot=*/false, m_LISTENTrigmode));
    http_conn::m_epoller = &m_epoller;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);

    utils.setnonblocking(m_pipefd[0]);
    m_epoller.add(m_pipefd[0],
                  Epoller::makeEvents(/*want_read=*/true, /*one_shot=*/false, 0));

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    Utils::u_pipefd = m_pipefd;

    m_conn_mgr->setTimerService(&m_timer);
    m_conn_mgr->setEpoller(&m_epoller);
    m_conn_mgr->setCloseLog(m_close_log);

    // 启动定时器后台线程
    m_timer.start(TIMESLOT);
}

bool WebServer::dealclientdata()
{
    return m_acceptor->acceptAll(
        [this](int connfd, const sockaddr_in &addr) -> bool
        {
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                return false; // 停止本轮 accept
            }

            m_conn_mgr->add(connfd, addr, m_root, m_CONNTrigmode,
                            m_close_log, m_user, m_passWord, m_databaseName);
            return true; // 继续 accept
        });
}

bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    // [修改] timeout 参数已废弃（定时器已移至独立线程），保留参数以兼容头文件声明
    (void)timeout;
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

void WebServer::dealwithread(int sockfd)
{
    //reactor
    if (1 == m_actormodel)
    {
        m_conn_mgr->touch(sockfd);
        m_pool->append(m_conn_mgr->get(sockfd), 0);

        while (true)
        {
            if (1 == m_conn_mgr->get(sockfd)->improv)
            {
                if (1 == m_conn_mgr->get(sockfd)->timer_flag)
                {
                    m_conn_mgr->remove(sockfd);
                    m_conn_mgr->get(sockfd)->timer_flag = 0;
                }
                m_conn_mgr->get(sockfd)->improv = 0;
                break;
            }
        }
    }
    // procator
    else
    {
        if (m_conn_mgr->get(sockfd)->read_once())
        {
            LOG_INFO("deal with the client(%s)",
                     inet_ntoa(m_conn_mgr->get(sockfd)->get_address()->sin_addr));
            m_pool->append_p(m_conn_mgr->get(sockfd));
            m_conn_mgr->touch(sockfd);
        }
        else
        {
            m_conn_mgr->remove(sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd)
{
    // reactor
    if (1 == m_actormodel)
    {
        m_conn_mgr->touch(sockfd);
        m_pool->append(m_conn_mgr->get(sockfd), 1);

        while (true)
        {
            if (1 == m_conn_mgr->get(sockfd)->improv)
            {
                if (1 == m_conn_mgr->get(sockfd)->timer_flag)
                {
                    m_conn_mgr->remove(sockfd);
                    m_conn_mgr->get(sockfd)->timer_flag = 0;
                }
                m_conn_mgr->get(sockfd)->improv = 0;
                break;
            }
        }
    }
    else
    {
        // proactor
        if (m_conn_mgr->get(sockfd)->write())
        {
            LOG_INFO("send data to the client(%s)",
                     inet_ntoa(m_conn_mgr->get(sockfd)->get_address()->sin_addr));
            m_conn_mgr->touch(sockfd);
        }
        else
        {
            m_conn_mgr->remove(sockfd);
        }
    }
}

void WebServer::eventLoop()
{
    bool stop_server = false;
    bool timeout = false;

    while (!stop_server)
    {
        int number = m_epoller.wait(-1); // ← 改
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = m_epoller.dataFd(i);  // ← 改
            uint32_t ev = m_epoller.events(i); // ← 改

            if (sockfd == m_listenfd)
            {
                if (!dealclientdata())
                    continue;
            }
            else if (ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                m_conn_mgr->remove(sockfd);
            }
            else if (sockfd == m_pipefd[0] && (ev & EPOLLIN))
            {
                if (!dealwithsignal(timeout, stop_server))
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            else if (ev & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (ev & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
    }
}