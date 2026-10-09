#ifndef ACCEPTOR_H
#define ACCEPTOR_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <functional>

class Acceptor
{
public:
    using Callback = std::function<bool(int connfd, const sockaddr_in &addr)>;

    Acceptor(int listenfd, int trigmode); // trigmode: 0=LT, 1=ET
    ~Acceptor() {}

    Acceptor(const Acceptor &) = delete;
    Acceptor &operator=(const Acceptor &) = delete;

    void setCloseLog(int close_log) { m_close_log = close_log; }

    //   LT 模式：accept 成功且 cb 接受 → true；否则 false
    //   ET 模式：永远返回 false（表示本轮 accept 已完成）
    bool acceptAll(const Callback &cb);

private:
    int m_listenfd;
    int m_trigmode;
    int m_close_log;
};

#endif