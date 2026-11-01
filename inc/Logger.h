#pragma once

#include "Epoll.h"
#include "Socket.h"
#include "Thread.h"
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <sys/stat.h>

class CLoggerServer;
struct LogData
{
    void const* data;
    size_t size;
};
enum LogLevel
{
    LOG_INFO,
    LOG_DEBUG,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL
};

class LogInfo
{
public:
    LogInfo(char const* file,
            int line,
            char const* function,
            pid_t pid,
            pthread_t tid,
            int level,
            char const* format,
            ...);
    LogInfo(char const* file, int line, char const* function, pid_t pid, pthread_t tid, int level);
    LogInfo(char const* file,
            int line,
            char const* function,
            pid_t pid,
            pthread_t tid,
            int level,
            LogData data);
    ~LogInfo();
    operator Buffer() const
    {
        return m_buf;
    }
    template <typename T>
    LogInfo& operator<<(T const& data)
    {
        std::ostringstream stream;
        stream << data;
        m_buf += stream.str();
        return *this;
    }

private:
    bool bAuto = false;
    Buffer m_buf;
};

class CLoggerServer
{
public:
    CLoggerServer()
        : m_thread(&CLoggerServer::ThreadFunc, this), m_path("./log/" + GetTimeStr() + ".log")
    {
    }
    ~CLoggerServer()
    {
        Close();
    }
    CLoggerServer(CLoggerServer const&) = delete;
    CLoggerServer& operator=(CLoggerServer const&) = delete;
    int Start()
    {
        if (m_server)
            return -1;
        if (mkdir("log", 0755) != 0 && errno != EEXIST)
            return -2;
        m_file = fopen(m_path, "ab");
        if (!m_file)
            return -3;
        if (m_epoll.Create(1) != 0)
        {
            Close();
            return -4;
        }
        m_server = new CSocket();
        int result = m_server->Init(CSockParam("./log/server.sock", SOCK_ISSERVER));
        if (result != 0 || m_epoll.Add(*m_server, EpollData(m_server)) != 0)
        {
            Close();
            return -5;
        }
        m_running = true;
        if (m_thread.Start() != 0)
        {
            Close();
            return -6;
        }
        return 0;
    }
    int Close()
    {
        m_running = false;
        m_thread.Stop();
        m_epoll.Close();
        delete m_server;
        m_server = nullptr;
        if (m_file)
        {
            fclose(m_file);
            m_file = nullptr;
        }
        return 0;
    }
    static void Trace(LogInfo const& info)
    {
        static thread_local std::unique_ptr<CSocket> client;
        static thread_local pid_t owner = -1;
        if (!client || owner != getpid())
        {
            client = std::make_unique<CSocket>();
            owner = getpid();
            if (client->Init(CSockParam("./log/server.sock", 0)) != 0 || client->Link() != 0)
            {
                client.reset();
                return;
            }
        }
        if (client->Send(info) != 0)
            client.reset();
    }
    static Buffer GetTimeStr();

private:
    int ThreadFunc()
    {
        std::map<int, std::unique_ptr<CSocketBase>> clients;
        EPEvents events;
        while (m_running)
        {
            ssize_t count = m_epoll.WaitEvents(events, 50);
            if (count < 0)
                break;
            for (ssize_t i = 0; i < count; ++i)
            {
                auto* socket = static_cast<CSocketBase*>(events[i].data.ptr);
                if (socket == m_server)
                {
                    CSocketBase* client = nullptr;
                    if (m_server->Link(&client) != 0)
                        continue;
                    int descriptor = *client;
                    if (m_epoll.Add(descriptor, EpollData(client), EPOLLIN | EPOLLRDHUP) != 0)
                        delete client;
                    else
                        clients[descriptor].reset(client);
                }
                else if (socket)
                {
                    int descriptor = *socket;
                    Buffer data(4096);
                    int result = socket->Recv(data);
                    if (result <= 0)
                    {
                        m_epoll.Del(descriptor);
                        clients.erase(descriptor);
                    }
                    else
                        WriteLog(data);
                }
            }
        }
        return 0;
    }
    void WriteLog(Buffer const& data)
    {
        if (m_file)
        {
            fwrite(data.data(), 1, data.size(), m_file);
            fflush(m_file);
        }
    }
    CThread m_thread;
    CEpoll m_epoll;
    CSocketBase* m_server = nullptr;
    Buffer m_path;
    FILE* m_file = nullptr;
    std::atomic<bool> m_running{false};
};

#define TRACEI(...)               \
    CLoggerServer::Trace(LogInfo( \
        __FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_INFO, __VA_ARGS__))
#define TRACED(...)               \
    CLoggerServer::Trace(LogInfo( \
        __FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_DEBUG, __VA_ARGS__))
#define TRACEW(...)               \
    CLoggerServer::Trace(LogInfo( \
        __FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_WARNING, __VA_ARGS__))
#define TRACEE(...)               \
    CLoggerServer::Trace(LogInfo( \
        __FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_ERROR, __VA_ARGS__))
#define TRACEF(...)               \
    CLoggerServer::Trace(LogInfo( \
        __FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_FATAL, __VA_ARGS__))
#define LOGI LogInfo(__FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_INFO)
#define LOGD LogInfo(__FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_DEBUG)
#define LOGW LogInfo(__FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_WARNING)
#define LOGE LogInfo(__FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_ERROR)
#define LOGF LogInfo(__FILE__, __LINE__, __FUNCTION__, getpid(), pthread_self(), LOG_FATAL)
#define DUMPI(data, size)                        \
    CLoggerServer::Trace(LogInfo(__FILE__,       \
                                 __LINE__,       \
                                 __FUNCTION__,   \
                                 getpid(),       \
                                 pthread_self(), \
                                 LOG_INFO,       \
                                 LogData{data, size}))
#define DUMPD(data, size)                        \
    CLoggerServer::Trace(LogInfo(__FILE__,       \
                                 __LINE__,       \
                                 __FUNCTION__,   \
                                 getpid(),       \
                                 pthread_self(), \
                                 LOG_DEBUG,      \
                                 LogData{data, size}))
#define DUMPW(data, size)                        \
    CLoggerServer::Trace(LogInfo(__FILE__,       \
                                 __LINE__,       \
                                 __FUNCTION__,   \
                                 getpid(),       \
                                 pthread_self(), \
                                 LOG_WARNING,    \
                                 LogData{data, size}))
#define DUMPE(data, size)                        \
    CLoggerServer::Trace(LogInfo(__FILE__,       \
                                 __LINE__,       \
                                 __FUNCTION__,   \
                                 getpid(),       \
                                 pthread_self(), \
                                 LOG_ERROR,      \
                                 LogData{data, size}))
#define DUMPF(data, size)                        \
    CLoggerServer::Trace(LogInfo(__FILE__,       \
                                 __LINE__,       \
                                 __FUNCTION__,   \
                                 getpid(),       \
                                 pthread_self(), \
                                 LOG_FATAL,      \
                                 LogData{data, size}))
