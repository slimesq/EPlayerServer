
#pragma once

#include "Function.h"

class CProcess
{
public:
    CProcess()
    {
        m_func = NULL;
        memset(m_socketFds, 0, sizeof(m_socketFds));
    }
    ~CProcess()
    {
        if (m_func != NULL)
        {
            delete m_func;
            m_func = NULL;
        }
    }
    template <typename _FUNCTION_, typename... _ARGS_>
    int SetEntryFunction(_FUNCTION_ func, _ARGS_... args)
    {
        m_func = new CFunction(func, args...);
        return 0;
    }
    int CreateSubProcess()
    {
        if (m_func == NULL)
            return -1;
        int ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, m_socketFds);
        if (ret == -1)
            return -2;

        pid_t pid = fork();
        if (pid == -1)
            return -2;
        if (pid == 0)
        {
            // 子进程
            close(m_socketFds[1]);  // 保留端点0,用于读写
            m_socketFds[1] = 0;
            ret = (*m_func)();
            exit(0);
        }
        // 主进程
        close(m_socketFds[0]);  // 保留端点1,用于读写
        m_socketFds[0] = 0;
        m_pid = pid;
        return 0;
    }

    // 主进程完成
    int SendFD(int fd)
    {
        msghdr msg{};
        iovec iov[2];
        char buf[2][10] = {"edoyun", "jueding"};
        iov[0].iov_base = buf[0];
        iov[0].iov_len = sizeof(buf[0]);
        iov[1].iov_base = buf[1];
        iov[1].iov_len = sizeof(buf[1]);
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;

        // 下面的数据,才是我们需要传递的
        cmsghdr* cmsg = (cmsghdr*)(calloc(1, CMSG_LEN(sizeof(int))));
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        *(int*)CMSG_DATA(cmsg) = fd;
        msg.msg_control = cmsg;
        msg.msg_controllen = cmsg->cmsg_len;
        ssize_t ret = sendmsg(m_socketFds[1], &msg, 0);
        free(cmsg);
        if (ret == -1)
        {
            return -1;
        }
        return 0;
    }

    // 子进程
    int RecvFd(int& fd)
    {
        msghdr msg{};
        iovec iov[2];
        char buf[][10] = {"", ""};
        iov[0].iov_base = buf[0];
        iov[0].iov_len = sizeof(buf[0]);
        iov[1].iov_base = buf[1];
        iov[1].iov_len = sizeof(buf[1]);
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;

        cmsghdr* cmsg = (cmsghdr*)calloc(1, CMSG_LEN(sizeof(int)));
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        msg.msg_control = cmsg;
        msg.msg_controllen = CMSG_LEN(sizeof(int));
        ssize_t ret = recvmsg(m_socketFds[0], &msg, 0);
        if (ret == -1)
        {
            free(cmsg);
            return -2;
        }
        fd = *(int*)CMSG_DATA(cmsg);
        free(cmsg);
        return 0;
    }

    static int SwitchDeamon()
    {
        pid_t ret = fork();
        if (ret == -1)
            return -1;
        if (ret > 0)
            exit(0);  // 主进程到此为止
        // 子进程内容如下
        ret = setsid();
        if (ret == -1)
            return -2;  // 失败,则返回
        ret = fork();
        if (ret == -1)
            return -3;
        if (ret > 0)
            exit(0);  // 子进程到此为止
        for (int i = 0; i < 3; ++i)
            close(i);
        umask(0);
        signal(SIGCHLD, SIG_IGN);

        return 0;
    }

private:
    CFunctionBase* m_func;
    pid_t m_pid;
    int m_socketFds[2];
};