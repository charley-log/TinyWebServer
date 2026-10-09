
#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class sem
{
public:
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};
class locker
{
public:
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};
class cond
{
public:
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            // pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        // pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        // pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    // static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

// ========== RAII 锁管理（自动加锁/解锁） ==========
template <typename LockType>
class LockGuard
{
public:
    explicit LockGuard(LockType &lock) : m_lock(lock)
    {
        m_lock.lock();
    }
    ~LockGuard()
    {
        m_lock.unlock();
    }
    // 禁止拷贝
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;

private:
    LockType &m_lock;
};

template <typename LockType>
class LockGuardDeferred
{
public:
    explicit LockGuardDeferred(LockType &lock) : m_lock(lock), m_locked(false)
    {
    }
    ~LockGuardDeferred()
    {
        if (m_locked)
        {
            m_lock.unlock();
        }
    }
    void lock()
    {
        m_lock.lock();
        m_locked = true;
    }
    void unlock()
    {
        m_lock.unlock();
        m_locked = false;
    }
    bool owns_lock() const
    {
        return m_locked;
    }
    // 禁止拷贝
    LockGuardDeferred(const LockGuardDeferred &) = delete;
    LockGuardDeferred &operator=(const LockGuardDeferred &) = delete;

private:
    LockType &m_lock;
    bool m_locked;
};

template <typename LockType>
class UnlockThenLock
{
public:
    explicit UnlockThenLock(LockType &lock) : m_lock(lock)
    {
        m_lock.unlock();
    }
    ~UnlockThenLock()
    {
        m_lock.lock();
    }
    // 禁止拷贝
    UnlockThenLock(const UnlockThenLock &) = delete;
    UnlockThenLock &operator=(const UnlockThenLock &) = delete;

private:
    LockType &m_lock;
};
#endif