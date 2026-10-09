#ifndef UTILS_H
#define UTILS_H

#include <unistd.h>
#include <signal.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    // 对文件描述符设置非阻塞
    int setnonblocking(int fd);

    // 信号处理函数
    static void sig_handler(int sig);

    // 设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    void show_error(int connfd, const char *info);

    static int *u_pipefd;
};

#endif