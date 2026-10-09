#include "epoller.h"
#include <unistd.h>
#include <string.h>

Epoller::Epoller(int max_events)
    : m_epollfd(-1), m_max_events(max_events > 0 ? max_events : 10000)
{
    m_epollfd = epoll_create(1); // 参数在现代内核里被忽略
    m_events = new epoll_event[m_max_events];
    memset(m_events, 0, sizeof(epoll_event) * m_max_events);
}

Epoller::~Epoller()
{
    if (m_epollfd != -1)
        close(m_epollfd);
    delete[] m_events;
}

bool Epoller::add(int fd, uint32_t events)
{
    epoll_event ev;
    ev.data.fd = fd;
    ev.events = events;
    return epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool Epoller::mod(int fd, uint32_t events)
{
    epoll_event ev;
    ev.data.fd = fd;
    ev.events = events;
    return epoll_ctl(m_epollfd, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool Epoller::del(int fd)
{
    return epoll_ctl(m_epollfd, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int Epoller::wait(int timeout_ms)
{
    return epoll_wait(m_epollfd, m_events, m_max_events, timeout_ms);
}

uint32_t Epoller::makeEvents(bool want_read, bool one_shot, int trigmode)
{
    uint32_t ev = want_read ? EPOLLIN : EPOLLOUT;
    if (trigmode)
        ev |= EPOLLET;
    ev |= EPOLLRDHUP;
    if (one_shot)
        ev |= EPOLLONESHOT;
    return ev;
}