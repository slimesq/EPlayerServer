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

class Buffer : public std::string
{
public:
    Buffer() : std::string()
    {
    }
    Buffer(size_t size) : std::string()
    {
        resize(size);
    }
    operator char*()
    {
        return (char*)c_str();
    }
    operator char*() const
    {
        return (char*)c_str();
    }
    operator char const*() const
    {
        return c_str();
    }
};

enum SockAttr
{
    SOCK_ISSERVER = 1,    // 是否是服务器   1表示是服务器 0表示客户端
    SOCK_ISNONBLOCK = 2,  // 是否阻塞   1表示非阻塞 0表示阻塞
    SOCK_ISUDP = 4        // 是否为UDP 1表示UDP 0表示TCP
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
    CSockParam(Buffer const& ip, short port, int attr)
    {
        this->ip = ip;
        this->port = port;
        this->attr = attr;
        addr_in.sin_family = AF_INET;
        addr_in.sin_port = htons(port);
        addr_in.sin_addr.s_addr = inet_addr(ip);
    }
    CSockParam(Buffer const& path, int attr)
    {
        this->ip = path;
        addr_un.sun_family = AF_UNIX;
        strcpy(addr_un.sun_path, path);
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
        }
        return 0;
    }

protected:
    // 套接字描述符,默认是-1
    int m_socket;
    // 状态 0初始化  1初始化 2连接完成 3关闭
    int m_status;
    // 初始化参数
    CSockParam m_param;
};

class CLocalSocket : public CSocketBase
{
public:
    CLocalSocket() : CSocketBase()
    {
    }
    CLocalSocket(int sock) : CSocketBase()
    {
        m_socket = sock;
    }

    virtual ~CLocalSocket()  // 传递析构操作
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
            m_socket = socket(PF_LOCAL, type, 0);
            if (m_socket == -1)
            {
                return -2;
            }
        }

        int ret = 0;
        if (m_param.attr & SOCK_ISSERVER)
        {
            ret = bind(m_socket, m_param.addrun(), sizeof(sockaddr_un));
            if (ret == -1)
            {
                if (errno != EAGAIN)
                {
                    return -3;
                }
            }
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
        m_status = 1;
        return 0;
    }

    // 连接: 对udp这里可以忽略
    // -服务器: accept
    // -客户端：connect
    virtual int Link(CSocketBase** pClient = NULL)
    {
        if (m_status <= 0 && (m_socket == -1))
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
            socklen_t len = sizeof(sockaddr_un);
            bzero(&param.addr_un, len);
            int fd = accept(m_socket, param.addrun(), &len);
            if (fd == -1)
            {
                return -3;
            }
            *pClient = new CLocalSocket(fd);
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
            ret = connect(m_socket, m_param.addrun(), sizeof(sockaddr_un));
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
            ssize_t len = write(m_socket, (char*)data + index, data.size() - index);
            if (len == 0)
                return -2;
            if (len < 0)
                return -3;

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
        ssize_t len = read(m_socket, data, data.size());
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