#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <unordered_map>
#include <string>
#include "../http/http_conn.h"
#include "../timer/timer_service.h"
#include "../log/log.h"
#include "epoller.h"

using std::string;

class Epoller;
class ConnectionManager
{
public:
    ConnectionManager(int max_fd, int timeout_sec);
    ~ConnectionManager();

    ConnectionManager(const ConnectionManager &) = delete;
    ConnectionManager &operator=(const ConnectionManager &) = delete;

    //设置日志标志
    void setCloseLog(int close_log) { m_close_log = close_log; }

    // 依赖注入（在 WebServer::eventListen 末尾调用）
    void setTimerService(TimerService *ts) { m_timer = ts; }
    void setEpoller(Epoller *ep) { m_epoller = ep; }

    // 建立连接：初始化 http_conn + 注册超时定时器
    void add(int connfd, const sockaddr_in &addr,
             char *root, int trigmode, int close_log,
             const string &user, const string &passwd, const string &dbname);

    // 关闭连接（主动关闭 / 对端关闭 / 出错关闭 都走这里）
    void remove(int connfd);

    // 刷新活跃时间（收到数据时调用）
    void touch(int connfd);

    // 取连接对象
    http_conn *get(int connfd) { return &m_users[connfd]; }

    // 当前连接数
    int count() const { return http_conn::m_user_count.load(); }

    // 超时秒数（供外部参考）
    int timeout() const { return m_timeout_sec; }

private:
    // 超时和主动关闭共用的清理逻辑
    void closeInternal(int connfd);

    http_conn *m_users;
    std::unordered_map<int, TimerService::TimerId> m_timer_ids;
    TimerService *m_timer;
    Epoller *m_epoller;
    int m_max_fd;
    int m_timeout_sec;
    int m_close_log;
};

#endif