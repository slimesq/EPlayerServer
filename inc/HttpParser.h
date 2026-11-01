#pragma once
#include "Public.h"
#include "http_parser.h"
#include <map>

class CHttpParser
{
private:
    http_parser m_parser;
    http_parser_settings m_settings;
    std::map<Buffer, Buffer> m_HeaderValues;
    Buffer m_status;
    Buffer m_url;
    Buffer m_body;
    bool m_complete;
    bool m_lastWasValue = false;
    Buffer m_lastField;

public:
    CHttpParser();
    ~CHttpParser();
    CHttpParser(CHttpParser const& http);
    CHttpParser& operator=(CHttpParser const& http);

public:
    size_t Parser(Buffer const& data);
    //GET POST ... 参考http_parser.h HTTP_METHOD_MAP宏
    unsigned Method() const
    {
        return m_parser.method;
    }
    std::map<Buffer, Buffer> const& Headers()
    {
        return m_HeaderValues;
    }
    Buffer const& Status() const
    {
        return m_status;
    }
    Buffer const& Url() const
    {
        return m_url;
    }
    Buffer const& Body() const
    {
        return m_body;
    }
    unsigned Errno() const
    {
        return m_parser.http_errno;
    }
    bool Complete() const
    {
        return m_complete;
    }

protected:
    static int OnMessageBegin(http_parser* parser);
    static int OnUrl(http_parser* parser, char const* at, size_t length);
    static int OnStatus(http_parser* parser, char const* at, size_t length);
    static int OnHeaderField(http_parser* parser, char const* at, size_t length);
    static int OnHeaderValue(http_parser* parser, char const* at, size_t length);
    static int OnHeadersComplete(http_parser* parser);
    static int OnBody(http_parser* parser, char const* at, size_t length);
    static int OnMessageComplete(http_parser* parser);
    int OnMessageBegin();
    int OnUrl(char const* at, size_t length);
    int OnStatus(char const* at, size_t length);
    int OnHeaderField(char const* at, size_t length);
    int OnHeaderValue(char const* at, size_t length);
    int OnHeadersComplete();
    int OnBody(char const* at, size_t length);
    int OnMessageComplete();
};

class UrlParser
{
public:
    UrlParser(Buffer const& url);
    ~UrlParser()
    {
    }
    int Parser();
    Buffer operator[](Buffer const& name) const;
    Buffer Protocol() const
    {
        return m_protocol;
    }
    Buffer Host() const
    {
        return m_host;
    }
    //默认返回80
    int Port() const
    {
        return m_port;
    }
    void SetUrl(Buffer const& url);
    Buffer const Uri() const
    {
        return m_uri;
    }

private:
    Buffer m_url;
    Buffer m_protocol;
    Buffer m_host;
    Buffer m_uri;
    int m_port;
    std::map<Buffer, Buffer> m_values;
};
