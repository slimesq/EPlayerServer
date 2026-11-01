#pragma once

#include "Function.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

class CProcess
{
public:
    CProcess() = default;
    ~CProcess()
    {
        for (int descriptor : m_socketFds)
            if (descriptor >= 0)
                close(descriptor);
        delete m_func;
    }
    CProcess(CProcess const&) = delete;
    CProcess& operator=(CProcess const&) = delete;

    template <typename Function, typename... Args>
    int SetEntryFunction(Function function, Args... args)
    {
        if (m_pid > 0)
            return -1;
        auto* next = new CFunction<Function, Args...>(function, args...);
        delete m_func;
        m_func = next;
        return 0;
    }
    int CreateSubProcess()
    {
        if (!m_func || m_pid > 0)
            return -1;
        for (int& descriptor : m_socketFds)
        {
            if (descriptor >= 0)
                close(descriptor);
            descriptor = -1;
        }
        m_notified = false;
        if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, m_socketFds) != 0)
            return -2;
        m_pid = fork();
        if (m_pid < 0)
        {
            close(m_socketFds[0]);
            close(m_socketFds[1]);
            m_socketFds[0] = m_socketFds[1] = -1;
            return -3;
        }
        if (m_pid == 0)
        {
            close(m_socketFds[1]);
            m_socketFds[1] = -1;
            // Ctrl+C 由父进程统一处理，再通过 IPC 通知子进程清理并退出。
            signal(SIGINT, SIG_IGN);
            signal(SIGTERM, SIG_DFL);
            int result = (*m_func)();
            if (!m_notified)
                NotifyReady(result);
            _exit(result == 0 ? 0 : 1);
        }
        close(m_socketFds[0]);
        m_socketFds[0] = -1;
        return 0;
    }
    int SendFD(int descriptor)
    {
        return SendSocket(descriptor, nullptr);
    }
    int RecvFD(int& descriptor)
    {
        return RecvSocket(descriptor, nullptr);
    }
    int RecvFd(int& descriptor)
    {
        return RecvFD(descriptor);
    }

    int SendSocket(int descriptor, sockaddr_in const* address)
    {
        sockaddr_in payload{};
        if (address)
            payload = *address;
        iovec vector{&payload, sizeof(payload)};
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
        msghdr message{};
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        if (descriptor >= 0)
        {
            message.msg_control = control;
            message.msg_controllen = sizeof(control);
            cmsghdr* header = CMSG_FIRSTHDR(&message);
            header->cmsg_level = SOL_SOCKET;
            header->cmsg_type = SCM_RIGHTS;
            header->cmsg_len = CMSG_LEN(sizeof(int));
            memcpy(CMSG_DATA(header), &descriptor, sizeof(descriptor));
        }
        ssize_t sent;
        do
        {
            sent = sendmsg(m_socketFds[1], &message, MSG_NOSIGNAL);
        } while (sent < 0 && errno == EINTR);
        return sent == sizeof(payload) ? 0 : -1;
    }
    int RecvSocket(int& descriptor, sockaddr_in* address)
    {
        descriptor = -1;
        sockaddr_in payload{};
        iovec vector{&payload, sizeof(payload)};
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
        msghdr message{};
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        ssize_t received;
        do
        {
            received = recvmsg(m_socketFds[0], &message, MSG_CMSG_CLOEXEC);
        } while (received < 0 && errno == EINTR);
        if (received != sizeof(payload) || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)))
            return -1;
        if (address)
            *address = payload;
        for (cmsghdr* header = CMSG_FIRSTHDR(&message); header;
             header = CMSG_NXTHDR(&message, header))
        {
            if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS &&
                header->cmsg_len >= CMSG_LEN(sizeof(int)))
            {
                memcpy(&descriptor, CMSG_DATA(header), sizeof(descriptor));
                break;
            }
        }
        return 0;
    }
    int NotifyReady(int status)
    {
        m_notified = true;
        return send(m_socketFds[0], &status, sizeof(status), MSG_NOSIGNAL) == sizeof(status) ? 0
                                                                                             : -1;
    }
    int WaitReady(int timeout = 5000)
    {
        pollfd event{m_socketFds[1], POLLIN, 0};
        int result;
        do
        {
            result = poll(&event, 1, timeout);
        } while (result < 0 && errno == EINTR);
        if (result <= 0)
            return -1;
        int status = -1;
        return recv(m_socketFds[1], &status, sizeof(status), 0) == sizeof(status) ? status : -1;
    }
    int Wait(int timeout = 3000)
    {
        if (m_pid <= 0)
            return 0;
        int status = 0;
        for (int elapsed = 0; elapsed < timeout; elapsed += 10)
        {
            pid_t result = waitpid(m_pid, &status, WNOHANG);
            if (result == m_pid || (result < 0 && errno == ECHILD))
            {
                m_pid = -1;
                return status;
            }
            if (result < 0 && errno != EINTR)
                return -1;
            usleep(10000);
        }
        kill(m_pid, SIGTERM);
        while (waitpid(m_pid, &status, 0) < 0 && errno == EINTR)
        {
        }
        m_pid = -1;
        return status;
    }
    static int SwitchDeamon()
    {
        pid_t pid = fork();
        if (pid < 0)
            return -1;
        if (pid > 0)
            _exit(0);
        if (setsid() < 0)
            return -2;
        pid = fork();
        if (pid < 0)
            return -3;
        if (pid > 0)
            _exit(0);
        for (int i = 0; i < 3; ++i)
            close(i);
        umask(0);
        signal(SIGCHLD, SIG_IGN);
        return 0;
    }

private:
    CFunctionBase* m_func = nullptr;
    pid_t m_pid = -1;
    int m_socketFds[2]{-1, -1};
    bool m_notified = false;
};
