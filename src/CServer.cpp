#include "CServer.h"
#include "Logger.h"

CServer::CServer() : m_server(nullptr), m_business(nullptr)
{
}

int CServer::Init(CBusiness* business, Buffer const& ip, short port)
{
    if (!business || m_server)
        return -1;
    m_business = business;
    m_stop = 0;
    if (m_process.SetEntryFunction(&CBusiness::BusinessProcess, business, &m_process) != 0 ||
        m_process.CreateSubProcess() != 0)
        return -2;
    if (m_process.WaitReady() != 0)
    {
        Close();
        return -3;
    }
    m_server = new CSocket();
    if (m_server->Init(CSockParam(
            ip, port, SOCK_ISSERVER | SOCK_ISIP | SOCK_ISREUSE | SOCK_ISNONBLOCK)) != 0 ||
        m_epoll.Create(2) != 0 || m_epoll.Add(*m_server, EpollData(m_server)) != 0 ||
        m_pool.Start(2) != 0)
    {
        Close();
        return -4;
    }
    for (size_t i = 0; i < m_pool.Size(); ++i)
    {
        if (m_pool.AddTask(&CServer::ThreadFunc, this) != 0)
        {
            Close();
            return -5;
        }
    }
    return 0;
}

int CServer::Run()
{
    while (!m_stop && m_server)
        usleep(10000);
    return 0;
}

int CServer::Close()
{
    m_stop = 1;
    m_pool.Close();
    if (m_server)
    {
        m_epoll.Del(*m_server);
        delete m_server;
        m_server = nullptr;
    }
    m_epoll.Close();
    m_process.SendSocket(-1, nullptr);
    m_process.Wait();
    return 0;
}

int CServer::ThreadFunc()
{
    EPEvents events;
    while (!m_stop)
    {
        ssize_t count = m_epoll.WaitEvents(events, 50);
        if (count < 0)
            return -1;
        for (ssize_t i = 0; i < count && !m_stop; ++i)
        {
            if (!(events[i].events & EPOLLIN))
                continue;
            CSocketBase* client = nullptr;
            if (m_server->Link(&client) != 0)
                continue;
            int result = m_process.SendSocket(*client, *client);
            delete client;
            if (result != 0)
                TRACEE("Failed to hand client socket to business process");
        }
    }
    return 0;
}
