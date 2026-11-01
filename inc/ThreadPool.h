#pragma once

#include "Thread.h"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

class CThreadPool
{
public:
    CThreadPool() = default;
    ~CThreadPool()
    {
        Close();
    }
    CThreadPool(CThreadPool const&) = delete;
    CThreadPool& operator=(CThreadPool const&) = delete;

    int Start(unsigned count)
    {
        if (!m_threads.empty() || count == 0)
            return -1;
        m_stopping = false;
        for (unsigned i = 0; i < count; ++i)
        {
            auto thread = std::make_unique<CThread>(&CThreadPool::TaskDispatch, this);
            if (thread->Start() != 0)
            {
                Close();
                return -2;
            }
            m_threads.push_back(std::move(thread));
        }
        return 0;
    }
    void Close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_ready.notify_all();
        for (auto& thread : m_threads)
            thread->Stop();
        m_threads.clear();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tasks.clear();
    }
    template <typename Function, typename... Args>
    int AddTask(Function function, Args... args)
    {
        auto task = std::make_unique<CFunction<Function, Args...>>(function, args...);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping || m_threads.empty())
                return -1;
            m_tasks.push_back(std::move(task));
        }
        m_ready.notify_one();
        return 0;
    }
    size_t Size() const
    {
        return m_threads.size();
    }

private:
    int TaskDispatch()
    {
        for (;;)
        {
            std::unique_ptr<CFunctionBase> task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ready.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
                if (m_stopping && m_tasks.empty())
                    return 0;
                task = std::move(m_tasks.front());
                m_tasks.pop_front();
            }
            (*task)();
        }
    }
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<std::unique_ptr<CFunctionBase>> m_tasks;
    std::vector<std::unique_ptr<CThread>> m_threads;
    bool m_stopping = true;
};
