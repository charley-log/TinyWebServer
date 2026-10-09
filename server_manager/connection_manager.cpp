#include "connection_manager.h"

ConnectionManager::ConnectionManager(int max_fd, int timeout_sec)
    : m_users(new http_conn[max_fd]), m_timer(NULL), m_epoller(NULL), m_max_fd(max_fd), m_timeout_sec(timeout_sec), m_close_log(0)
{
}

ConnectionManager::~ConnectionManager()
{
    // 先取消所有还没触发的定时器，防止 TimerService 后续触发回调时
    // 访问已经析构的 this（WebServer 析构顺序无法保证）
    if (m_timer)
    {
        for (auto &kv : m_timer_ids)
            m_timer->cancel(kv.second);
    }
    m_timer_ids.clear();

    delete[] m_users;
    m_users = NULL;
}

void ConnectionManager::add(int connfd, const sockaddr_in &addr,
                            char *root, int trigmode, int close_log,
                            const string &user, const string &passwd, const string &dbname)
{
    // 1) 初始化连接对象（原 WebServer::timer 第一段）
    m_users[connfd].init(connfd, addr, root, trigmode, close_log,
                         user, passwd, dbname);

    // 2) fd 复用保护：旧定时器还在，先取消
    auto it = m_timer_ids.find(connfd);
    if (it != m_timer_ids.end())
    {
        m_timer->cancel(it->second);
        m_timer_ids.erase(it);
    }

    // 3) 注册超时回调：捕获 connfd，走 closeInternal 统一清理
    TimerService::TimerId id = m_timer->addTimer(m_timeout_sec, [this, connfd]()
                                                 { closeInternal(connfd); });
    m_timer_ids[connfd] = id;
}

void ConnectionManager::remove(int connfd)
{
    closeInternal(connfd);
}

void ConnectionManager::touch(int connfd)
{
    auto it = m_timer_ids.find(connfd);
    if (it != m_timer_ids.end())
        m_timer->refresh(it->second, m_timeout_sec);

    LOG_INFO("%s", "adjust timer once");
}

void ConnectionManager::closeInternal(int connfd)
{
    // 1) 取消 / 清理定时器句柄
    auto it = m_timer_ids.find(connfd);
    if (it != m_timer_ids.end())
    {
        // 超时路径下 TimerService 内部已经摘过该节点，cancel 找不到就安全返回
        m_timer->cancel(it->second);
        m_timer_ids.erase(it);
    }

    // 2) 从 epoll 移除并关闭 fd
    m_epoller->del(connfd);
    close(connfd);

    // 3) 计数减一
    http_conn::m_user_count--;

    // 4) 与原 deal_timer / 超时回调一致的日志
    LOG_INFO("close fd %d", connfd);
}