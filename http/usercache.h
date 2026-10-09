
#ifndef USERCACHE_H
#define USERCACHE_H

#include <map>
#include <string>
#include <mutex>
#include <utility>

using namespace std;

class UserCache
{
public:
    // 线程安全查找，找到返回true并填充passwd
    bool find(const string &user, string &passwd)
    {
        lock_guard<mutex> lock(m_mutex);
        auto it = users_.find(user);
        if (it != users_.end())
        {
            passwd = it->second;
            return true;
        }
        return false;
    }

    // 线程安全判断用户是否存在
    bool exists(const string &user)
    {
        lock_guard<mutex> lock(m_mutex);
        return users_.find(user) != users_.end();
    }

    // 线程安全插入/更新
    void insert(const string &user, const string &passwd)
    {
        lock_guard<mutex> lock(m_mutex);
        users_[user] = passwd;
    }

    // 原子性检查+插入，返回是否插入成功（用户不存在时插入）
    // 用于注册场景，避免"先查再插"的竞态条件
    bool try_insert(const string &user, const string &passwd)
    {
        lock_guard<mutex> lock(m_mutex);
        auto it = users_.find(user);
        if (it != users_.end())
        {
            return false; // 用户已存在
        }
        users_[user] = passwd;
        return true;
    }

    // 线程安全删除
    void remove(const string &user)
    {
        lock_guard<mutex> lock(m_mutex);
        users_.erase(user);
    }

    // 线程安全清空
    void clear()
    {
        lock_guard<mutex> lock(m_mutex);
        users_.clear();
    }

private:
    map<string, string> users_;
    mutex m_mutex;
};

#endif