#pragma once

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <fcntl.h>
#include "Public.h"

enum SockAttr
{
    SOCK_ISSERVER = 1,    // 是否是服务器   1表示是服务器 0表示客户端
    SOCK_ISNONBLOCK = 2,  // 是否阻塞   1表示非阻塞 0表示阻塞
    SOCK_ISUDP = 4,       // 是否为UDP 1表示UDP 0表示TCP
    SOCK_ISIP = 8,
    SOCK_ISREUSE = 16
};

class CSockParam
{
public:
    CSockParam()
    {
        bzero(&addr_in, sizeof(addr_in));
        bzero(&addr_un, sizeof(addr_un));
        port = -1;
        attr = 0;  // 默认是客户端,阻塞,TCP
    }
    CSockParam(Buffer const& ip, short port, int attr) : CSockParam()
    {
        this->ip = ip;
        this->port = port;
        this->attr = attr;
        addr_in.sin_family = AF_INET;
        addr_in.sin_port = htons(port);
        addr_in.sin_addr.s_addr = inet_addr(ip);
    }
    CSockParam(sockaddr_in const* address, int attr) : CSockParam()
    {
        addr_in = *address;
        ip = inet_ntoa(address->sin_addr);
        port = ntohs(address->sin_port);
        this->attr = attr;
    }
    CSockParam(Buffer const& path, int attr) : CSockParam()
    {
        this->ip = path;
        addr_un.sun_family = AF_UNIX;
        if (path.size() < sizeof(addr_un.sun_path))
            memcpy(addr_un.sun_path, path.c_str(), path.size() + 1);
        this->attr = attr;
    }
    ~CSockParam()
    {
    }
    CSockParam(CSockParam const& param)
    {
        ip = param.ip;
        port = param.port;
        attr = param.attr;
        memcpy(&addr_in, &param.addr_in, sizeof(addr_in));
        memcpy(&addr_un, &param.addr_un, sizeof(addr_un));
    }

public:
    CSockParam& operator=(CSockParam const& param)
    {
        if (this != &param)
        {
            ip = param.ip;
            port = param.port;
            attr = param.attr;
            memcpy(&addr_in, &param.addr_in, sizeof(addr_in));
            memcpy(&addr_un, &param.addr_un, sizeof(addr_un));
        }
        return *this;
    }
    sockaddr* addrin()
    {
        return (sockaddr*)&addr_in;
    }
    sockaddr* addrun()
    {
        return (sockaddr*)&addr_un;
    }

public:
    // 地址
    sockaddr_in addr_in;
    sockaddr_un addr_un;
    // ip
    Buffer ip;
    // port
    short port;
    // 参考SockAttr
    int attr;
};

class CSocketBase
{
public:
    CSocketBase()
    {
        m_socket = -1;
        m_status = 0;  // 初始化未完成
    }
    virtual ~CSocketBase()  // 传递析构操作
    {
        Close();
    }

public:
    // 初始化:
    // -服务器: 套接字创建、bind、listen
    // -客户端：套接字创建
    virtual int Init(CSockParam const& param) = 0;

    // 连接: 对udp这里可以忽略
    // -服务器: accept
    // -客户端：connect
    virtual int Link(CSocketBase** pClient = NULL) = 0;

    // 发送数据
    virtual int Send(Buffer const& data) = 0;

    // 接受数据
    virtual int Recv(Buffer& buffer) = 0;

    // 关闭连接
    virtual int Close()
    {
        m_status = 3;  // 3表示关闭
        if (m_socket != -1)
        {
            int fd = m_socket;
            m_socket = -1;
            close(fd);
            if (m_ownsPath)
                unlink(m_param.ip);
            m_ownsPath = false;
        }
        return 0;
    }

    virtual operator int()
    {
        return m_socket;
    }
    virtual operator int() const
    {
        return m_socket;
    }
    virtual operator sockaddr_in*()
    {
        return &m_param.addr_in;
    }
    virtual operator sockaddr_in const*() const
    {
        return &m_param.addr_in;
    }

protected:
    // 套接字描述符,默认是-1
    bool m_ownsPath = false;
    int m_socket;
    // 状态 0初始化  1初始化 2连接完成 3关闭
    int m_status;
    // 初始化参数
    CSockParam m_param;
};

class CSocket : public CSocketBase
{
public:
    CSocket() : CSocketBase()
    {
    }
    CSocket(int sock) : CSocketBase()
    {
        m_socket = sock;
    }

    virtual ~CSocket()  // 传递析构操作
    {
        Close();
    }

public:
    // 初始化:
    // -服务器: 套接字创建、bind、listen
    // -客户端：套接字创建
    virtual int Init(CSockParam const& param)
    {
        if (m_status != 0)
        {
            return -1;
        }
        m_param = param;
        int type = (m_param.attr & SOCK_ISUDP) ? SOCK_DGRAM : SOCK_STREAM;
        if (m_socket == -1)
        {
            m_socket = socket((param.attr & SOCK_ISIP) ? PF_INET : PF_LOCAL, type, 0);
            if (m_socket == -1)
            {
                return -2;
            }
        }

        else
        {
            m_status = 2;
        }

        int ret = 0;
        if ((m_param.attr & SOCK_ISREUSE) && (m_param.attr & SOCK_ISIP))
        {
            int reuse = 1;
            if (setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
                return -7;
        }
        if (!(m_param.attr & SOCK_ISIP) && m_param.ip.size() >= sizeof(m_param.addr_un.sun_path))
            return -8;
        if (m_param.attr & SOCK_ISSERVER)
        {
            ret = (m_param.attr & SOCK_ISIP)
                ? bind(m_socket, m_param.addrin(), sizeof(sockaddr_in))
                : bind(m_socket, m_param.addrun(), sizeof(sockaddr_un));
            if (ret == -1)
            {
                return -3;
            }
            m_ownsPath = !(m_param.attr & SOCK_ISIP);
            ret = listen(m_socket, 32);
            if (ret == -1)
            {
                return -4;
            }
        }

        if (m_param.attr & SOCK_ISNONBLOCK)
        {
            int option = fcntl(m_socket, F_GETFL);
            if (option == -1)
            {
                return -5;
            }
            option |= O_NONBLOCK;
            ret = fcntl(m_socket, F_SETFL, option);
            if (ret == -1)
            {
                return -6;
            }
        }
        if (m_status == 0)
            m_status = 1;
        return 0;
    }

    // 连接: 对udp这里可以忽略
    // -服务器: accept
    // -客户端：connect
    virtual int Link(CSocketBase** pClient = NULL)
    {
        if (m_status <= 0 || (m_socket == -1))
        {
            return -1;
        }

        int ret = 0;
        if (m_param.attr & SOCK_ISSERVER)
        {
            if (pClient == NULL)
            {
                return -2;
            }
            CSockParam param;
            param.attr = m_param.attr & SOCK_ISIP;
            socklen_t len = (param.attr & SOCK_ISIP) ? sizeof(sockaddr_in) : sizeof(sockaddr_un);
            int fd =
                accept(m_socket, (param.attr & SOCK_ISIP) ? param.addrin() : param.addrun(), &len);
            if (fd == -1)
            {
                return -3;
            }
            *pClient = new CSocket(fd);
            if (*pClient == NULL)
            {
                return -4;
            }
            ret = (*pClient)->Init(param);
            if (ret != 0)
            {
                delete (*pClient);
                *pClient = NULL;
                return -5;
            }
        }
        else
        {
            ret = (m_param.attr & SOCK_ISIP)
                ? connect(m_socket, m_param.addrin(), sizeof(sockaddr_in))
                : connect(m_socket, m_param.addrun(), sizeof(sockaddr_un));
            if (ret != 0)
            {
                return -6;
            }
        }
        m_status = 2;
        return 0;
    }

    // 发送数据
    virtual int Send(Buffer const& data)
    {
        if (m_status < 2 || m_socket == -1)
        {
            return -1;
        }
        ssize_t index = 0;
        while (index < (ssize_t)data.size())
        {
            ssize_t len = send(m_socket, data.data() + index, data.size() - index, MSG_NOSIGNAL);
            if (len == 0)
                return -2;
            if (len < 0)
            {
                if (errno == EINTR)
                    continue;
                return -3;
            }

            index += len;
        }

        return 0;
    }

    // 接受数据: 大于0 表示接收成功;小于0 表示失败;等于0 表示没有收到数据
    virtual int Recv(Buffer& data)
    {
        if (m_status < 2 || m_socket == -1)
        {
            return -1;
        }
        if (data.empty())
            data.resize(1024 * 1024);
        ssize_t len;
        do
        {
            len = read(m_socket, data, data.size());
        } while (len < 0 && errno == EINTR);
        if (len > 0)
        {
            data.resize(len);
            return (int)len;  // 收到数据
        }
        if (len < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                data.clear();
                return 0;  // 没有收到数据
            }
            return -2;  // 发生错误
        }

        return -3;  // 套接字被关闭
    }

    // 关闭连接
    virtual int Close()
    {
        return CSocketBase::Close();
    }
};

using CLocalSocket = CSocket;
