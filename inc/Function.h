#pragma once

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <functional>
#include <utility>
#include <memory.h>
#include <signal.h>

class CSocketBase;
class Buffer;

class CFunctionBase
{
public:
    virtual ~CFunctionBase()
    {
    }
    virtual int operator()()
    {
        return -1;
    }
    virtual int operator()(CSocketBase*)
    {
        return -1;
    }
    virtual int operator()(CSocketBase*, Buffer const&)
    {
        return -1;
    }
};

template <typename _FUNCTION_, typename... _ARGS_>
class CFunction : public CFunctionBase
{
public:
    CFunction(_FUNCTION_ func, _ARGS_... args)
        : m_binder{std::forward<_FUNCTION_>(func), std::forward<_ARGS_>(args)...}
    {
    }
    virtual ~CFunction()
    {
    }
    virtual int operator()()
    {
        return m_binder();
    }
    typename std::_Bindres_helper<int, _FUNCTION_, _ARGS_...>::type m_binder;
};
