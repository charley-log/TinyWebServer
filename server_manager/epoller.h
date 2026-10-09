#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h>
#include <stdint.h>

class Epoller
{
public:
    explicit Epoller(int max_events = 10000);
    ~Epoller();

    Epoller(const Epoller &) = delete;
    Epoller &operator=(const Epoller &) = delete;

    // 原始 epoll fd，供 Utils / http_conn 兼容使用
    int fd() const { return m_epollfd; }

    bool add(int fd, uint32_t events);
    bool mod(int fd, uint32_t events);
    bool del(int fd);

    // 返回就绪事件数；-1 表示错误（errno 保留）
    int wait(int timeout_ms);

    // 访问第 i 个就绪事件
    uint32_t events(int i) const { return m_events[i].events; }
    int dataFd(int i) const { return m_events[i].data.fd; }

    // 便捷构造事件掩码
    static uint32_t makeEvents(bool want_read, bool one_shot, int trigmode);

private:
    int m_epollfd;
    int m_max_events;
    epoll_event *m_events;
};

#endif