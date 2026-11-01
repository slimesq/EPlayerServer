#pragma once

#include "Function.h"
#include <atomic>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <mutex>
#include <pthread.h>
#include <unistd.h>

class CThread
{
public:
    CThread() = default;
    template <typename Function, typename... Args>
    CThread(Function function, Args... args) : CThread()
    {
        SetThreadFunc(function, args...);
    }
    ~CThread()
    {
        Stop();
        delete m_function;
    }
    CThread(CThread const&) = delete;
    CThread& operator=(CThread const&) = delete;

    template <typename Function, typename... Args>
    int SetThreadFunc(Function function, Args... args)
    {
        if (m_joinable)
            return -1;
        auto* next = new CFunction<Function, Args...>(function, args...);
        delete m_function;
        m_function = next;
        return 0;
    }
    int Start()
    {
        if (m_joinable || !m_function)
            return -1;
        std::call_once(m_signalOnce, [] {
            struct sigaction action{};
            action.sa_handler = &CThread::PauseSignal;
            sigemptyset(&action.sa_mask);
            sigaction(SIGUSR1, &action, nullptr);
        });
        m_running = true;
        int result = pthread_create(&m_thread, nullptr, &CThread::ThreadEntry, this);
        if (result != 0)
        {
            m_running = false;
            return -result;
        }
        m_joinable = true;
        return 0;
    }
    int Pause()
    {
        if (!m_running)
            return -1;
        bool paused = !m_paused.load();
        m_paused = paused;
        if (paused)
        {
            int result = pthread_kill(m_thread, SIGUSR1);
            if (result)
            {
                m_paused = false;
                return -result;
            }
        }
        return 0;
    }
    int Stop()
    {
        if (!m_joinable)
            return 0;
        if (pthread_equal(pthread_self(), m_thread))
            return -1;
        m_running = false;
        m_paused = false;
        timespec deadline{};
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += 100000000;
        if (deadline.tv_nsec >= 1000000000)
        {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000;
        }
        int result = pthread_timedjoin_np(m_thread, nullptr, &deadline);
        if (result == ETIMEDOUT)
        {
            pthread_cancel(m_thread);
            result = pthread_join(m_thread, nullptr);
        }
        if (result != 0)
            return -result;
        m_joinable = false;
        m_thread = 0;
        return 0;
    }
    bool isValid() const
    {
        return m_running.load();
    }

private:
    static void* ThreadEntry(void* argument)
    {
        auto* self = static_cast<CThread*>(argument);
        m_current = self;
        (*self->m_function)();
        self->m_running = false;
        m_current = nullptr;
        return nullptr;
    }
    static void PauseSignal(int)
    {
        while (m_current && m_current->m_paused && m_current->m_running)
            usleep(1000);
    }
    CFunctionBase* m_function = nullptr;
    pthread_t m_thread = 0;
    bool m_joinable = false;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};
    static thread_local CThread* m_current;
    static std::once_flag m_signalOnce;
};
