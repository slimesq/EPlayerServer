#pragma once

#include <asm-generic/errno-base.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <errno.h>
#include <sys/signal.h>

#define EVENT_SIZE 128

class EpollData
{
public:
    EpollData()
    {
        m_data.u64 = 0;
    }
    EpollData(void* ptr)
    {
        m_data.ptr = ptr;
    }
    explicit EpollData(int fd)
    {
        m_data.fd = fd;
    }
    explicit EpollData(uint32_t u32)
    {
        m_data.u32 = u32;
    }
    explicit EpollData(uint64_t u64)
    {
        m_data.u64 = u64;
    }
    EpollData(EpollData const& data)
    {
        m_data.u64 = data.m_data.u64;
    }

public:
    EpollData& operator=(EpollData const& data)
    {
        if (this != &data)
        {
            m_data.u64 = data.m_data.u64;
        }
        return *this;
    }
    EpollData& operator=(void* data)
    {
        m_data.ptr = data;
        return *this;
    }
    EpollData& operator=(int fd)
    {
        m_data.fd = fd;
        return *this;
    }
    EpollData& operator=(uint32_t u32)
    {
        m_data.u32 = u32;
        return *this;
    }
    EpollData& operator=(uint64_t u64)
    {
        m_data.u64 = u64;
        return *this;
    }

    operator epoll_data_t()
    {
        return m_data;
    }
    operator epoll_data_t() const
    {
        return m_data;
    }
    operator epoll_data_t*()
    {
        return &m_data;
    }
    operator epoll_data_t const*() const
    {
        return &m_data;
    }

private:
    epoll_data_t m_data;
};

using EPEvents = std::vector<epoll_event>;

class CEpoll
{
public:
    CEpoll()
    {
        m_epoll = -1;
    };
    ~CEpoll()
    {
        Close();
    }

public:
    CEpoll(CEpoll const&) = delete;
    CEpoll& operator=(CEpoll const&) = delete;

public:
    int Create(unsigned count)
    {
        if (m_epoll != -1)
            return -1;
        m_epoll = epoll_create(count);
        if (m_epoll == -1)
        {
            return -2;
        }
        return 0;
    }
    // 小于0表示错误 等于0表示没有事情发生 大于0表示成功拿到事件
    ssize_t WaitEvents(EPEvents& events, int timeout = 10)
    {
        if (m_epoll == -1)
            return -1;
        EPEvents evs{EVENT_SIZE};
        int ret = epoll_wait(m_epoll, evs.data(), (int)evs.size(), timeout);
        if (ret == -1)
        {
            if ((errno == EINTR) || errno == EAGAIN)
            {
                return 0;
            }
            return -2;
        }
        if (ret > (int)evs.size())
        {
            evs.resize(ret);
        }
        memcpy(events.data(), evs.data(), sizeof(epoll_event) * ret);
        return ret;
    }
    int Add(int fd, EpollData const& data = EpollData((void*)0), uint32_t events = EPOLLIN)
    {
        if (m_epoll == -1)
            return -1;
        epoll_event ev{events, data};
        int ret = epoll_ctl(m_epoll, EPOLL_CTL_ADD, fd, &ev);
        if (ret == -1)
        {
            return -2;
        }

        return 0;
    }
    int modify(int fd, uint32_t events, EpollData const& data = EpollData((void*)0))
    {
        if (m_epoll == -1)
            return -1;
        epoll_event ev{events, data};
        int ret = epoll_ctl(m_epoll, EPOLL_CTL_MOD, fd, &ev);
        if (ret == -1)
        {
            return -2;
        }

        return 0;
    }
    int Del(int fd)
    {
        if (m_epoll == -1)
            return -1;
        int ret = epoll_ctl(m_epoll, EPOLL_CTL_DEL, fd, NULL);
        if (ret == -1)
        {
            return -2;
        }
        return 0;
    }
    void Close()
    {
        if (m_epoll != -1)
        {
            int fd = m_epoll;
            m_epoll = -1;
            close(fd);
        }
    }

private:
    int m_epoll;
};