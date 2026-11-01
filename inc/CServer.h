#pragma once
#include "Socket.h"
#include "Epoll.h"
#include "ThreadPool.h"
#include "Process.h"
#include <atomic>

template <typename _FUNCTION_, typename... _ARGS_>
class CConnectedFunction : public CFunctionBase
{
public:
    CConnectedFunction(_FUNCTION_ func, _ARGS_... args)
        : m_binder(std::forward<_FUNCTION_>(func), std::forward<_ARGS_>(args)...)
    {
    }
    virtual ~CConnectedFunction()
    {
    }
    virtual int operator()(CSocketBase* pClient)
    {
        return m_binder(pClient);
    }
    typename std::_Bindres_helper<int, _FUNCTION_, _ARGS_...>::type m_binder;
};

template <typename _FUNCTION_, typename... _ARGS_>
class CReceivedFunction : public CFunctionBase
{
public:
    CReceivedFunction(_FUNCTION_ func, _ARGS_... args)
        : m_binder(std::forward<_FUNCTION_>(func), std::forward<_ARGS_>(args)...)
    {
    }
    virtual ~CReceivedFunction()
    {
    }
    virtual int operator()(CSocketBase* pClient, Buffer const& data)
    {
        return m_binder(pClient, data);
    }
    typename std::_Bindres_helper<int, _FUNCTION_, _ARGS_...>::type m_binder;
};

class CBusiness
{
public:
    CBusiness() : m_connectedcallback(NULL), m_recvcallback(NULL)
    {
    }
    virtual ~CBusiness()
    {
        delete m_connectedcallback;
        delete m_recvcallback;
    }
    virtual int BusinessProcess(CProcess* proc) = 0;
    template <typename _FUNCTION_, typename... _ARGS_>
    int setConnectedCallback(_FUNCTION_ func, _ARGS_... args)
    {
        delete m_connectedcallback;
        m_connectedcallback = new CConnectedFunction<_FUNCTION_, _ARGS_...>(func, args...);
        if (m_connectedcallback == NULL)
            return -1;
        return 0;
    }
    template <typename _FUNCTION_, typename... _ARGS_>
    int setRecvCallback(_FUNCTION_ func, _ARGS_... args)
    {
        delete m_recvcallback;
        m_recvcallback = new CReceivedFunction<_FUNCTION_, _ARGS_...>(func, args...);
        if (m_recvcallback == NULL)
            return -1;
        return 0;
    }

protected:
    CFunctionBase* m_connectedcallback;
    CFunctionBase* m_recvcallback;
};

class CServer
{
public:
    CServer();
    ~CServer()
    {
        Close();
    }
    CServer(CServer const&) = delete;
    CServer& operator=(CServer const&) = delete;

public:
    int Init(CBusiness* business, Buffer const& ip = "0.0.0.0", short port = 9999);
    int Run();
    void RequestStop()
    {
        m_stop = 1;
    }
    int Close();

private:
    int ThreadFunc();

private:
    CThreadPool m_pool;
    CSocketBase* m_server;
    CEpoll m_epoll;
    CProcess m_process;
    CBusiness* m_business;  //业务模块由调用方管理
    sig_atomic_t volatile m_stop = 0;
};
