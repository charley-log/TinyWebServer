#include "acceptor.h"
#include "../log/log.h"
#include <errno.h>

Acceptor::Acceptor(int listenfd, int trigmode)
    : m_listenfd(listenfd), m_trigmode(trigmode), m_close_log(0)
{
}

bool Acceptor::acceptAll(const Callback &cb)
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    // LT：只 accept 一次
    if (0 == m_trigmode)
    {
        int connfd = accept(m_listenfd,
                            (struct sockaddr *)&client_address,
                            &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        return cb(connfd, client_address);
    }

    // ET：循环 accept 直到 EAGAIN 或 cb 拒绝
    while (true)
    {
        int connfd = accept(m_listenfd,
                            (struct sockaddr *)&client_address,
                            &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            break;
        }
        if (!cb(connfd, client_address))
            break;
    }
    return false; // ET 模式恒返回 false，与原逻辑一致
}