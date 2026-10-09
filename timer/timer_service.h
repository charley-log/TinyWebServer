#ifndef TIMER_SERVICE_H
#define TIMER_SERVICE_H

#include <time.h>
#include <stdint.h>
#include <pthread.h>
#include <vector>
#include <unordered_map>
#include <functional>
#include <unistd.h>

class TimerService
{
public:
    using TimerId = uint64_t;
    using Callback = std::function<void()>; // 用 lambda 捕获上下文，不需要参数

    TimerService();
    ~TimerService();

    TimerService(const TimerService &) = delete;
    TimerService &operator=(const TimerService &) = delete;

    // 启动后台线程，每 interval 秒 tick 一次
    void start(int interval);
    // 停止线程
    void stop();

    // 注册：expire_sec 秒后触发 cb，返回句柄；失败返回 0
    TimerId addTimer(time_t expire_sec, Callback cb);
    // 刷新：从当前时刻起重新计时 expire_sec 秒（时间只增不减）
    void refresh(TimerId id, time_t expire_sec);
    // 取消：若已触发或已取消，安全忽略
    void cancel(TimerId id);

private:
    struct Node
    {
        TimerId id;
        time_t expire;
        Callback cb;
        int heap_idx; // -1 表示已不在堆中
    };

    // 堆操作
    void siftUp(int idx);
    void siftDown(int idx);
    void swapNode(int i, int j);
    bool less(int i, int j) const;
    void detach(Node *n); // 从堆和 map 中摘除，不 delete

    // 线程入口
    static void *threadFunc(void *arg);
    void tick();

    pthread_mutex_t m_mutex;
    std::vector<Node *> m_heap;
    std::unordered_map<TimerId, Node *> m_map;
    TimerId m_next_id;

    pthread_t m_tid;
    bool m_running;
    int m_interval;
};

#endif