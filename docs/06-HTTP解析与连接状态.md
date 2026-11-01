# EPlayerServer 阶段六 HTTP 解析与连接状态

本章从业务进程已经登记客户端开始，跟踪一次读取怎样推进整条连接的解析状态。重点是消息边界、临时缓冲、持久状态、回调桥接，以及真正触发登录的位置。

源码入口：[HttpParser.h](../inc/HttpParser.h)、[HttpParser.cpp](../src/HttpParser.cpp)、[ClientState](../inc/EdoyunPlayerServer.h)、[业务实现](../src/EdoyunPlayerServer.cpp)、[http-parser](../third_party/http_parser/http_parser.h)。

## 1. TCP 的片段与 HTTP 的消息

TCP 只提供有序字节流，不保留发送调用边界。一条请求可拆成多次读取，多条请求也可在一次读取中出现。HTTP 用请求行、头、空行和消息体定界建立协议消息。

```text
HTTP 消息：GET /login ... + headers + body
TCP 交付：  片段 A | 片段 B | 片段 C
应用读取：  chunk 1 | chunk 2
```

这三组边界没有一一对应。Content-Length 等协议规则由解析器处理，不能用一次 Recv 的长度直接判定整条请求是否完整。语法可参考 [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html)。

| 消息部分 | 读者应看懂的内容 |
| --- | --- |
| 请求行 | 方法、请求目标和版本，例如 GET、/login?...、HTTP/1.1 |
| 头部 | Host、Content-Length 等名称和值，每行由 CRLF 结束 |
| 空行 | 结束头部，后面才可能是消息体 |
| 消息体 | 是否存在及如何定界由协议规则决定，不能只看当前缓冲还有多少字节 |

HTTP 解析器是一种状态机：它记住已经读到请求的哪个位置，再用下一段字节继续推进。例如正在读头字段值时，新片段应接在该值后面；只有进入新消息时才重置状态。

## 2. 连接状态的三个成员

当前 ClientState 为：

```cpp
struct ClientState
{
    std::shared_ptr<CSocketBase> socket;
    CHttpParser parser;
    Buffer request;
};
```

| 成员或变量 | 寿命 | 内容 |
| --- | --- | --- |
| socket | 连接状态期间 | 访问内核连接的包装 |
| parser | 多次事件之间持续 | 语法进度与已提取字段 |
| request | 多次事件之间持续 | 原始请求累计字节 |
| ThreadFunc 中的 chunk | 当前一次事件处理 | 本次 read 得到的字节 |
| ThreadFunc 中的 state | 当前一次事件处理 | 延长 ClientState 寿命的引用 |

没有持久 parser，会丢失前一片段的语法状态；没有累计 request，当前接收回调便无法得到完整原始请求。

## 3. CHttpParser 包装了什么

| 字段 | 责任 |
| --- | --- |
| m_parser | 底层 C 状态机 |
| m_settings | 底层回调函数表 |
| m_url、m_body | 累计 URL 和正文片段 |
| m_HeaderValues | 按头名称保存值 |
| m_lastField、m_lastWasValue | 连接被拆开的字段名与值 |
| m_complete | 当前消息是否走到完成回调 |

构造函数将底层设置为 HTTP_REQUEST，并让 parser.data 指回 this。Status() 虽然存在，保存的却是响应状态描述回调数据；当前请求主线一般为空，不是业务 JSON status，也不是响应的 200。

## 4. C API 回调如何到达对象

以 URL 为例：

```cpp
int CHttpParser::OnUrl(http_parser* parser, char const* at, size_t length)
{
    return ((CHttpParser*)parser->data)->OnUrl(at, length);
}
```

```cpp
int CHttpParser::OnUrl(char const* at, size_t length)
{
    m_url.append(at, length);
    return 0;
}
```

静态回调满足 C 库的普通函数指针要求，再根据 data 找回当前对象。每条连接有自己的 parser，因此回调更新的是当前连接的字段。

| 参数 | 含义 |
| --- | --- |
| parser | 当前底层解析实例，data 指向包装对象 |
| at | 当前片段首地址，不保证终止零 |
| length | 当前片段有效字节数 |

append 复制显式范围。回调返回后，原输入缓冲可以结束当前使用，不会因为 m_url 还需要内容而继续借用 at。

## 5. Parser、Errno、Complete 的区别

```cpp
size_t CHttpParser::Parser(Buffer const& data)
{
    return http_parser_execute(&m_parser, &m_settings, data.data(), data.size());
}
```

| 观察值 | 回答的问题 |
| --- | --- |
| Parser 返回值 | 本次输入消费了多少字节 |
| Errno() | 是否有语法或回调错误 |
| Complete() | 当前保存的消息是否完成 |
| Method() | 已解析的方法枚举 |
| Url()/Headers()/Body() | 当前提取出的字段 |

无错误的半条消息仍然不完整；正数消费量也不代表登录成功。业务 ThreadFunc 当前用 Errno 与 Complete 决定分支，没有建立按消费位置切分多请求的队列。

## 6. 三个片段的逐步推演

示例只为说明解析机制，POST 后续会被当前登录业务拒绝：

```text
片段 A: POST /login HTTP/1.1\r\nHost: local
片段 B: host\r\nContent-Length: 5\r\n\r\nhe
片段 C: llo
```

这里 `\r\n` 表示两个实际字节，空行是连续两个 CRLF。

| 到达点 | Host | Body | Complete |
| --- | --- | --- | --- |
| A 后 | local，仍可继续追加 | 空 | false |
| B 后 | localhost | he | false |
| C 后 | localhost | hello | true |

每次调用同一个 parser。若 B 到达时重新构造解析器，它会把 host 开头当成新 HTTP 消息，而不能继续前一个 Host 值。

## 7. 字段名和值如何跨回调拼接

```cpp
int CHttpParser::OnHeaderField(char const* at, size_t length)
{
    if (m_lastWasValue)
        m_lastField.clear();
    m_lastField.append(at, length);
    m_lastWasValue = false;
    return 0;
}
```

```cpp
int CHttpParser::OnHeaderValue(char const* at, size_t length)
{
    m_HeaderValues[m_lastField].append(at, length);
    m_lastWasValue = true;
    return 0;
}
```

字段名可能收到 `Content-`、`Length` 两次，值可能收到 `loc`、`alhost` 两次。m_lastWasValue 用于识别从前一个字段值切到新字段名的时刻，只有这时才清 m_lastField。

当前 map 保留头名称原样，没有统一大小写，也未定义完整重复头合并规则。底层识别 HTTP 语法，不等于包装已经提供通用的全部 HTTP 语义。

## 8. 开始、完成和复制

OnMessageBegin 清 URL、Body、Headers、字段拼接状态，并置 m_complete=false。OnMessageComplete 置 true。

复制包装时，源码复制底层结构和字符串后，重新将 m_parser.data 指向新的 this。如果漏掉这一步，新对象的回调会继续写原对象，出现“看起来复制了状态，却更新另一份对象”的错误。

这属于维护复制不变量，不是简单增加 shared_ptr 能解决的问题。当前复制路径用于保持解析器对象自己的状态，连接主线则主要持有并持续使用同一个实例。

## 9. 业务事件循环的真实分支

```cpp
int CEdoyunPlayerServer::ThreadFunc()
{
    EPEvents events;
    while (!m_stopping)
    {
        ssize_t count = m_epoll.WaitEvents(events, 50);
        if (count < 0)
            return -1;
        for (ssize_t i = 0; i < count; ++i)
        {
            int descriptor = events[i].data.fd;
            std::shared_ptr<ClientState> state;
            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                auto found = m_mapClients.find(descriptor);
                if (found != m_mapClients.end())
                    state = found->second;
            }
            if (!state)
                continue;
            if (!(events[i].events & EPOLLIN))
            {
                RemoveClient(descriptor);
                continue;
            }
            Buffer chunk(8192);
            int received = state->socket->Recv(chunk);
            if (received < 0)
            {
                RemoveClient(descriptor);
                continue;
            }
            if (received > 0)
            {
                state->request += chunk;
                state->parser.Parser(chunk);
            }
            if (state->request.size() > 1024 * 1024 || state->parser.Errno())
            {
                state->socket->Send(MakeResponse(-1));
                RemoveClient(descriptor);
            }
            else if (state->parser.Complete())
            {
                if (m_recvcallback)
                    (*m_recvcallback)(state->socket.get(), state->request);
                RemoveClient(descriptor);
            }
            else if (m_epoll.Modify(descriptor,
                                    EPOLLIN | EPOLLRDHUP | EPOLLONESHOT,
                                    EpollData(descriptor)) != 0)
                RemoveClient(descriptor);
        }
    }
    return 0;
}
```

### 9.1 先取得有效状态

根据 data.fd 查 map，在 map 锁内复制 shared_ptr，解锁后处理。没有查到就跳过。map 锁只保护容器，不包住 read、解析或数据库查询。

### 9.2 只追加真实字节

Recv 将 chunk 缩为实际大小，received>0 才累积并 Parser。received==0 表示包装层的暂不可读，不等于 EOF；负数则移除。

### 9.3 三个主要出口

| 条件 | 当前动作 | 后续 |
| --- | --- | --- |
| 请求累计超过 1 MiB 或解析错误 | 尝试发 -1 响应，RemoveClient | 本轮不再 rearm |
| Complete 为 true | 调接收回调，RemoveClient | 不再处理此连接的后续请求 |
| 仍未完整 | Modify 重新启用 ONESHOT | 等下一段 |

若 Modify 失败也移除。控制路径并不保证每种错误都成功返回 JSON，例如对端断开会直接结束。

## 10. 两个工作线程先后处理同一连接

```text
线程 A 取得事件，ONESHOT 禁用登记
  → 从 map 拿 shared_ptr
  → read 片段 A，更新 request 和 parser
  → 未完成，Modify 重启登记
  → 结束本轮，不再修改这份状态

线程 B 取得下一次事件
  → 拿到同一个 ClientState
  → read 片段 B，继续 parser
  → 完整，执行业务
```

shared_ptr 让 A/B 不因 map 删除立即失去对象，ONESHOT 与更新顺序限制同一登记的本轮并发。二者解决的问题不同，不能互相替代。

## 11. 为什么业务又解析一次

ClientState 的 parser 用于驱动连接何时进入完整请求分支。Received 接受 Buffer，所以调用业务 HttpParser 时又创建临时 CHttpParser 重新解释完整 request。

```text
增量 parser：解决什么时候可以调用业务
累计 request：提供回调目前要求的原始输入
临时 parser：为业务获取 method、URL 等字段
```

这是当前接口安排带来的重复解析和存储成本。将来若传递已解析请求对象，可以减少重复，但必须重新定义对象所有权和协议错误责任；当前并没有这个新接口。

## 12. URL 分解与参数解码

业务把 `http://127.0.0.1` 加在请求目标前，交给 UrlParser。它只是给 URL 工具提供完整形状，不会再建立网络连接。

UrlParser 从底层解析结果取协议、主机、端口、路径和 query，去掉首个路径斜杠。缺省端口 https 为 443，其他情况为 80；识别 https 文本不表示服务器实现了 TLS。

### 12.1 拆分必须早于解码

```text
query 原文 user=a%26b&salt=x%2By
  → 先分 user=a%26b 和 salt=x%2By
  → 再各自解码
  → user 为 a&b，salt 为 x+y
```

若先把 %26 变成 & 再拆，会把用户值误当成参数分隔。当前实现拒绝残缺/非法百分号编码和 %00，将 + 变为空格，重复参数由后值覆盖前值；缺失字段返回空字符串。

同一个 Decode 也用于路径，因此字面 + 同样变空格，是当前简化，不能当成所有 URI 场景的普遍规则。

## 13. 多请求输入的实际行为

底层 execute 可以在同一缓冲继续解析下一条消息。包装的 OnMessageBegin 会覆盖前一条字段和完成标志：

```text
一次输入包含完整 /first + 完整 /second
  → 最后保存 /second，Complete=true

一次输入包含完整 /first + 半条 /second
  → 最后保存 /second 的部分状态，Complete=false
```

应用没有在第一次完成时暂停解析，也没有切出第一条消息独立处理。因此“响应后关闭”描述常规单请求路径，不保证流水线输入总是只执行第一条请求。

## 14. RemoveClient 与最后一次 close

RemoveClient 先 Del，再在锁内擦 map。当前工作线程还持有局部 state，真正析构可能到本轮结束才发生，随后 socket 共享引用释放并关闭 FD。

逻辑移除、内存对象销毁和内核连接释放是三个时刻。当前没有为未完整请求设置完整超时回收，也没有全局连接上限；1 MiB 是累计请求限制，不是整个进程内存限额。

## 15. 本章与下一章的接口

连接层收齐字节并调用 Received，业务层再从完整请求取出 method、URL 和解码参数。到这里还未产生用户记录或登录结果。阶段七先解释表和字段怎样表示数据，阶段八再解释查询如何生成独立行，阶段九把它们接回登录。

[返回路线](README.md) · [上一章 日志系统](05-日志系统完整链路.md) · [下一章 数据库建模](07-数据库接口与元数据.md)
