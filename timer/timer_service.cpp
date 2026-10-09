#include "timer_service.h"

TimerService::TimerService()
    : m_next_id(1), m_tid(0), m_running(false), m_interval(0)
{
    pthread_mutex_init(&m_mutex, NULL);
}

TimerService::~TimerService()
{
    stop();
    // 清掉还留在堆里的节点
    for (size_t i = 0; i < m_heap.size(); ++i)
        delete m_heap[i];
    m_heap.clear();
    m_map.clear();
    pthread_mutex_destroy(&m_mutex);
}

// ---------------- 线程管理 ----------------

void TimerService::start(int interval)
{
    if (m_running)
        return;
    m_interval = interval;
    m_running = true;
    pthread_create(&m_tid, NULL, &TimerService::threadFunc, this);
}

void TimerService::stop()
{
    if (!m_running)
        return;
    m_running = false;
    pthread_join(m_tid, NULL);
    m_tid = 0;
}

void *TimerService::threadFunc(void *arg)
{
    TimerService *self = static_cast<TimerService *>(arg);
    while (self->m_running)
    {
        sleep(self->m_interval);
        if (!self->m_running)
            break;
        self->tick();
    }
    return NULL;
}

// ---------------- 对外接口 ----------------

TimerService::TimerId TimerService::addTimer(time_t expire_sec, Callback cb)
{
    pthread_mutex_lock(&m_mutex);

    Node *n = new Node;
    n->id = m_next_id++;
    n->expire = time(NULL) + expire_sec;
    n->cb = cb;
    n->heap_idx = (int)m_heap.size();

    m_heap.push_back(n);
    m_map[n->id] = n;
    siftUp(n->heap_idx);

    TimerId id = n->id;
    pthread_mutex_unlock(&m_mutex);
    return id;
}

void TimerService::refresh(TimerId id, time_t expire_sec)
{
    pthread_mutex_lock(&m_mutex);
    auto it = m_map.find(id);
    if (it != m_map.end())
    {
        Node *n = it->second;
        n->expire = time(NULL) + expire_sec; // 时间只增不减
        siftDown(n->heap_idx);               // 所以只需向下调整
    }
    pthread_mutex_unlock(&m_mutex);
}

void TimerService::cancel(TimerId id)
{
    pthread_mutex_lock(&m_mutex);
    auto it = m_map.find(id);
    if (it != m_map.end())
    {
        Node *n = it->second;
        detach(n);
        delete n;
    }
    pthread_mutex_unlock(&m_mutex);
}

// ---------------- 内部：tick + 堆操作 ----------------

void TimerService::tick()
{
    // 1) 加锁，把到期节点摘出来（不 delete）
    std::vector<Node *> expired;
    pthread_mutex_lock(&m_mutex);

    time_t cur = time(NULL);
    while (!m_heap.empty() && cur >= m_heap[0]->expire)
    {
        Node *top = m_heap[0];
        detach(top);
        expired.push_back(top);
    }

    pthread_mutex_unlock(&m_mutex);

    // 2) 解锁后执行回调（回调里可以安全地 addTimer/cancel/refresh）
    for (size_t i = 0; i < expired.size(); ++i)
    {
        if (expired[i]->cb)
            expired[i]->cb();
        delete expired[i];
    }
}

void TimerService::detach(Node *n)
{
    int idx = n->heap_idx;
    if (idx < 0)
    {
        m_map.erase(n->id);
        return;
    }

    // 与堆尾交换
    swapNode(idx, (int)m_heap.size() - 1);
    m_heap.pop_back();

    // 若移除的不是最后一个，调整堆
    if (idx < (int)m_heap.size())
    {
        if (idx > 0 && less(idx, (idx - 1) / 2))
            siftUp(idx);
        else
            siftDown(idx);
    }

    n->heap_idx = -1;
    m_map.erase(n->id);
}

void TimerService::swapNode(int i, int j)
{
    Node *tmp = m_heap[i];
    m_heap[i] = m_heap[j];
    m_heap[j] = tmp;
    m_heap[i]->heap_idx = i;
    m_heap[j]->heap_idx = j;
}

void TimerService::siftUp(int idx)
{
    while (idx > 0)
    {
        int parent = (idx - 1) / 2;
        if (less(idx, parent))
        {
            swapNode(idx, parent);
            idx = parent;
        }
        else
            break;
    }
}

void TimerService::siftDown(int idx)
{
    int n = (int)m_heap.size();
    while (2 * idx + 1 < n)
    {
        int child = 2 * idx + 1;
        if (child + 1 < n && less(child + 1, child))
            child++;
        if (less(child, idx))
        {
            swapNode(idx, child);
            idx = child;
        }
        else
            break;
    }
}

bool TimerService::less(int i, int j) const
{
    return m_heap[i]->expire < m_heap[j]->expire;
}