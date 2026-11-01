# EPlayerServer 阶段三 Socket 与 Epoll 事件驱动

本章从一个有效 socket FD 开始，说明地址、监听、连接、字节收发与事件登记怎样配合。重点是把“现在可以尝试 I/O”与“请求已经处理完成”分开。

源码入口：[Socket.h](../inc/Socket.h)、[Epoll.h](../inc/Epoll.h)、[CServer.cpp](../src/CServer.cpp)、[业务 ThreadFunc](../src/EdoyunPlayerServer.cpp)。

## 1. 三个对象各管什么

| 对象 | 保存内容 | 主要责任 |
| --- | --- | --- |
| CSockParam | IPv4 或 Unix 地址、端口、属性位 | 提供初始化参数 |
| CSocket | FD、状态、参数、Unix 路径拥有标志 | 调用 socket API 并关闭资源 |
| CEpoll | epoll 自己的 FD | 登记连接并等待就绪 |

socket FD 与 epoll FD 是不同内核资源。关闭 epoll 不代表自动关闭所有被监视 socket，删除登记也不等于删除 C++ 包装对象。

## 2. 地址与属性怎样进入初始化

### 2.1 两类地址

IPv4 使用 sockaddr_in：地址族、网络字节序端口和 IP。Unix 使用 sockaddr_un：地址族和本地路径。

父进程监听调用中包含 IP、端口和 `SOCK_ISSERVER | SOCK_ISIP | SOCK_ISREUSE | SOCK_ISNONBLOCK`；日志监听则使用路径与 SOCK_ISSERVER，不设置 SOCK_ISIP。

### 2.2 位标志不是互斥枚举

| 标志 | 当前用途 |
| --- | --- |
| SOCK_ISSERVER | 创建绑定和监听端，Link 选择 accept |
| SOCK_ISNONBLOCK | Init 中设置 O_NONBLOCK |
| SOCK_ISUDP | 选择数据报类型的标志，当前服务主线不使用 |
| SOCK_ISIP | 选择 IPv4，否则选择 Unix 域 |
| SOCK_ISREUSE | 对 IPv4 设置 SO_REUSEADDR |

按位或组合多个条件，按位与检查某一条件。SO_REUSEADDR 与父进程两个线程共享监听 FD 是不同机制；它不是为每个工作线程创建独立监听，也不是一套负载均衡策略。

### 2.3 监听端和连接端怎样配合

bind 为服务端指定本地地址，listen 让它接收连接请求。客户端 connect 后，TCP 完成建立连接的协议交互；服务端再用 accept 从待取队列得到一个已连接 FD。监听 FD 留在原处继续服务，之后的 read/send 使用新 FD。

阻塞调用在条件未满足时等待；非阻塞调用会及时返回“现在无法完成”，通常通过 -1 和 errno=EAGAIN 表示。EINTR 则表示调用被信号打断。后面的封装分别决定是否重试、稍后再等事件，或结束连接，因此必须结合包装层返回值阅读。

## 3. Init 的完整资源路径

```cpp
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
```

从前向后可以分成五步：检查状态、保存参数、必要时创建 FD、按服务端属性绑定监听、按非阻塞属性设置 flags。

### 3.1 新建 FD 与接管 FD

默认构造时 m_socket=-1，Init 会调用 socket。`CSocket(fd)` 则表示包装一个已有描述符，Init 不再创建另一个连接，而是把其状态视为已连接并设置参数。

业务接收父进程传来的 FD 正走第二条路径。它没有再次 connect，也没有复制出新的 TCP 会话。

### 3.2 bind、listen 和路径责任

服务端 bind 后调用 listen，当前 backlog 参数为 32。backlog 面向尚未 accept 的连接排队，不是已连接客户端总数限制。

Unix bind 成功后 m_ownsPath 才标记为 true。Close 仅由拥有路径的监听包装对象 unlink 路径；客户端关闭自己的连接，不应删除服务端名称。程序没有在 bind 前任意删除已存在路径，所以上次异常退出遗留的 server.sock 可能导致启动失败。

### 3.3 O_NONBLOCK 的设置方式

先 F_GETFL 取旧 flags，再按位或 O_NONBLOCK，用 F_SETFL 写回。这样保留其他已有状态位，而不是用一个新值覆盖全部 flags。

## 4. Link 的两条分支

```cpp
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
```

| 分支 | 参数要求 | 实际操作 |
| --- | --- | --- |
| 监听端 | pClient 非空 | accept，创建新包装，写回指针 |
| 主动连接端 | 不需要输出 client | connect 到保存的地址 |

监听 FD 在 accept 后继续监听，新 FD 代表特定客户端。原监听对象 m_status 变为已连接状态，并不表示它失去监听功能；这里状态字段是包装实现中的简化状态，不能完全等同为内核 socket 状态机。

### 4.1 为什么监听非阻塞，客户端还要重新设置

Linux accept 不自动继承监听 FD 的 O_NONBLOCK。当前代码创建 accepted 参数时，也只保留 SOCK_ISIP。父进程接入后暂不读取业务字节，转交到业务进程后再 Init 为非阻塞。规则见 [accept 手册](https://man7.org/linux/man-pages/man2/accept.2.html)。

### 4.2 两条接入线程可能同时被唤醒

一个线程先 accept 了连接，另一个随后调用时可能已经无连接可取。非阻塞监听使后者以错误返回而不是永久阻塞；CServer 当前对 Link 失败直接跳过，不终止整个服务循环。

## 5. Send 与部分写入

```cpp
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
```

### 5.1 三个长度

假设待发 100 字节：

| 步骤 | index | send 输入长度 | 本次写入 |
| --- | ---: | ---: | ---: |
| 初始 | 0 | 100 | 40 |
| 下一轮 | 40 | 60 | 60 |
| 结束 | 100 | 不再调用 | 总计完成 |

index 表示本函数已交给内核的字节数。`data.data()+index` 指向剩余开头，`data.size()-index` 限定剩余范围。

### 5.2 返回值的边界

Send 返回 0 表示内部循环已处理完输入字节，不表示客户端业务已解析，也不表示客户端一定收到 JSON 后报告成功。

EINTR 会重试。非阻塞 socket 的 EAGAIN 则直接返回失败；index 是局部变量，函数结束后没有把余量保存进连接状态。当前也没有登记 EPOLLOUT 后续写的逻辑。

因此它能处理“这次调用里发生的部分写入”，但没有完整的“跨事件继续发送”状态机。把 I/O 改为非阻塞，并不能单独保证输出不会丢尾部。

## 6. Recv 的包装语义

```cpp
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
```

| 底层 read 结果 | 包装返回 | Buffer 变化 |
| --- | --- | --- |
| 大于 0 | 实际字节数 | resize 为实际长度 |
| EINTR | 在内部重试 | 尚不交给上层 |
| EAGAIN | 0 | clear |
| 0，即 EOF | -3 | 上层应结束连接 |
| 其他错误 | 负数 | 交给上层处理 |

函数名是 Recv，底层实际用 read。这里不能把系统 read 的“0 表示 EOF”直接套到包装返回值上。

输入 Buffer 为空时会先扩为 1 MiB；业务明确创建 Buffer(8192)，因此使用的是 8192 字节接收空间。累计请求超过 1 MiB 的限制由业务层检查，和本函数的默认分配不是同一个条件。

## 7. epoll 解决哪种等待

没有数据时持续遍历所有连接并 read，会造成空转。epoll 让内核保存关注集合，线程在 epoll_wait 等待，只取回当前就绪条目。

```text
Add(FD, events, userData)
  → epoll_wait
  → 取回就绪 FD 对应的 userData
  → 应用调用 read/accept/send
  → 根据状态保留、修改或删除登记
```

通知的是就绪，应用仍完成 I/O。就绪不是完整 HTTP，也不是已完成的异步读操作。这是本项目 Reactor 思路的基础。

## 8. CEpoll 创建和事件容量

```cpp
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
```

当前使用旧 epoll_create 接口，count 必须为正，现代 Linux 忽略这个容量提示。因此父进程 Create(2) 不表示最多只支持两条连接，见 [epoll_create 手册](https://man7.org/linux/man-pages/man2/epoll_create.2.html)。

WaitEvents 使用的 EVENT_SIZE=128 是单次取回上限，也不是总连接数。更多就绪事件可以留到后续等待返回。

## 9. WaitEvents 的有效范围

```cpp
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
    if (ret > (int)events.size())
    {
        events.resize(ret);
    }
    if (ret > 0)
        memcpy(events.data(), evs.data(), sizeof(epoll_event) * ret);
    return ret;
}
```

| 返回值 | 上层行为 |
| --- | --- |
| 小于 0 | 包装错误，当前长期循环可能返回 |
| 0 | 无有效事件，可能超时或被信号打断 |
| 大于 0 | 仅处理 `[0, ret)` 的条目 |

输出 vector 只在不足时扩容，不会随 ret 变小：

```text
第一轮 ret=12 → events.size() 至少 12
第二轮 ret=2  → events.size() 仍可为 12
第二轮有效条目只有前 2 个
```

若遍历 vector 全长，会把旧事件再次当作新事件。当前循环使用 count，而不是 events.size()。

## 10. Add、Modify 和 Del

Add 为 FD 建立登记，Modify 改关注事件和用户数据，Del 移除登记。三个操作都通过 epoll_ctl 完成。

EpollData 包装 epoll_data_t 联合体。父接入和日志登记对象指针，业务登记整数 FD：

| 登记值 | 读取方式 | 寿命责任 |
| --- | --- | --- |
| EpollData(m_server) | events[i].data.ptr | 包装对象必须仍有效 |
| EpollData(descriptor) | events[i].data.fd | 再从 map 找连接状态 |

内核不会对裸指针增加引用计数，也不理解 C++ 动态类型。登记和对象拥有关系必须由应用自己连接。

## 11. LT、ET 与 ONESHOT

### 11.1 当前使用 LT

LT（Level Triggered，水平触发）在条件持续满足时可以再次返回事件。父进程一次事件只 accept 一次，若仍有待取连接，后续等待仍有机会返回。ET（Edge Triggered，边沿触发）关注条件变化，通常需要一直处理到 EAGAIN，否则余下数据可能没有新的边沿。

源码没有使用 EPOLLET，因此不能按 ET 的约束解释当前所有循环。

### 11.2 ONESHOT 暂停的是登记

业务客户端使用 `EPOLLIN | EPOLLRDHUP | EPOLLONESHOT`：

| 标志 | 本次登记关注或采用的规则 |
| --- | --- |
| EPOLLIN | 可读，例如有数据、待取连接或读端结束等条件 |
| EPOLLRDHUP | 对端关闭连接或关闭写方向 |
| EPOLLONESHOT | 交付事件后停用登记，等待显式重新启用 |

```text
登记 → 事件交付并禁用该登记
      → 一个线程读取、更新 parser
      ├─ 完整或失败：删除
      └─ 未完整：Modify 重新启用
```

ONESHOT 与 LT/ET 属于不同维度。它让多个等待线程协调同一连接本轮处理；不是所有连接共享一把全局锁，也不能保证连接对象任意成员的线程安全。语义见 [epoll 手册](https://man7.org/linux/man-pages/man7/epoll.7.html)。

对端关闭写方向后，本端读完缓存可能看到 EOF。当前处理代码先看是否带 EPOLLIN，有数据时仍尝试读取，并非一见 RDHUP 就丢弃全部缓存字节。

## 12. 删除登记与关闭 FD

业务 RemoveClient 先 Del，再从 map 擦除。工作线程若还有局部 shared_ptr，socket 暂时继续存活；最后一个状态引用释放时才 Close。

父监听则先停止工作线程，再删登记和 delete 包装，避免循环继续使用失效对象。日志的 ptr 数据同样要求先停止使用登记再释放包装。

## 13. 当前网络封装的范围

UDP 标志只影响 socket 类型选择，当前服务端 Init 仍执行 listen，不能据此认定已支持完整 UDP 服务。登录主线是 IPv4 TCP，日志是 Unix 流，控制是 Unix 顺序分组。

接收限制、连接额度和发送余量分别属于不同资源问题，阶段六和十一会结合 ClientState 继续解释。当前阅读本章应掌握的是：Socket 负责实际收发，Epoll 负责等待机会，服务循环负责决定下一步。

下一章解释工作线程怎样被创建，以及“长期事件循环”怎样通过通用任务队列启动。

[返回路线](README.md) · [上一章 进程与 FD](02-进程与文件描述符传递.md) · [下一章 回调与任务池](04-回调线程与任务池.md)
