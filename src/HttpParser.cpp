#include "HttpParser.h"
#include <cstring>
#include <cstdlib>

CHttpParser::CHttpParser()
{
    m_complete = false;
    memset(&m_parser, 0, sizeof(m_parser));
    m_parser.data = this;
    http_parser_init(&m_parser, HTTP_REQUEST);
    memset(&m_settings, 0, sizeof(m_settings));
    m_settings.on_message_begin = &CHttpParser::OnMessageBegin;
    m_settings.on_url = &CHttpParser::OnUrl;
    m_settings.on_status = &CHttpParser::OnStatus;
    m_settings.on_header_field = &CHttpParser::OnHeaderField;
    m_settings.on_header_value = &CHttpParser::OnHeaderValue;
    m_settings.on_headers_complete = &CHttpParser::OnHeadersComplete;
    m_settings.on_body = &CHttpParser::OnBody;
    m_settings.on_message_complete = &CHttpParser::OnMessageComplete;
}

CHttpParser::~CHttpParser()
{
}

CHttpParser::CHttpParser(CHttpParser const& http)
{
    memcpy(&m_parser, &http.m_parser, sizeof(m_parser));
    m_parser.data = this;
    memcpy(&m_settings, &http.m_settings, sizeof(m_settings));
    m_status = http.m_status;
    m_url = http.m_url;
    m_body = http.m_body;
    m_complete = http.m_complete;
    m_lastField = http.m_lastField;
    m_lastWasValue = http.m_lastWasValue;
    m_HeaderValues = http.m_HeaderValues;
}

CHttpParser& CHttpParser::operator=(CHttpParser const& http)
{
    if (this != &http)
    {
        memcpy(&m_parser, &http.m_parser, sizeof(m_parser));
        m_parser.data = this;
        memcpy(&m_settings, &http.m_settings, sizeof(m_settings));
        m_status = http.m_status;
        m_url = http.m_url;
        m_body = http.m_body;
        m_complete = http.m_complete;
        m_lastField = http.m_lastField;
        m_lastWasValue = http.m_lastWasValue;
        m_HeaderValues = http.m_HeaderValues;
    }
    return *this;
}

size_t CHttpParser::Parser(Buffer const& data)
{
    return http_parser_execute(&m_parser, &m_settings, data.data(), data.size());
}

int CHttpParser::OnMessageBegin(http_parser* parser)
{
    return ((CHttpParser*)parser->data)->OnMessageBegin();
}

int CHttpParser::OnUrl(http_parser* parser, char const* at, size_t length)
{
    return ((CHttpParser*)parser->data)->OnUrl(at, length);
}

int CHttpParser::OnStatus(http_parser* parser, char const* at, size_t length)
{
    return ((CHttpParser*)parser->data)->OnStatus(at, length);
}

int CHttpParser::OnHeaderField(http_parser* parser, char const* at, size_t length)
{
    return ((CHttpParser*)parser->data)->OnHeaderField(at, length);
}

int CHttpParser::OnHeaderValue(http_parser* parser, char const* at, size_t length)
{
    return ((CHttpParser*)parser->data)->OnHeaderValue(at, length);
}

int CHttpParser::OnHeadersComplete(http_parser* parser)
{
    return ((CHttpParser*)parser->data)->OnHeadersComplete();
}

int CHttpParser::OnBody(http_parser* parser, char const* at, size_t length)
{
    return ((CHttpParser*)parser->data)->OnBody(at, length);
}

int CHttpParser::OnMessageComplete(http_parser* parser)
{
    return ((CHttpParser*)parser->data)->OnMessageComplete();
}

int CHttpParser::OnMessageBegin()
{
    m_complete = false;
    m_HeaderValues.clear();
    m_url.clear();
    m_status.clear();
    m_body.clear();
    m_lastField.clear();
    m_lastWasValue = false;
    return 0;
}

int CHttpParser::OnUrl(char const* at, size_t length)
{
    m_url.append(at, length);
    return 0;
}

int CHttpParser::OnStatus(char const* at, size_t length)
{
    m_status.append(at, length);
    return 0;
}

int CHttpParser::OnHeaderField(char const* at, size_t length)
{
    if (m_lastWasValue)
        m_lastField.clear();
    m_lastField.append(at, length);
    m_lastWasValue = false;
    return 0;
}

int CHttpParser::OnHeaderValue(char const* at, size_t length)
{
    m_HeaderValues[m_lastField].append(at, length);
    m_lastWasValue = true;
    return 0;
}

int CHttpParser::OnHeadersComplete()
{
    return 0;
}

int CHttpParser::OnBody(char const* at, size_t length)
{
    m_body.append(at, length);
    return 0;
}

int CHttpParser::OnMessageComplete()
{
    m_complete = true;
    return 0;
}

namespace
{
int HexDigit(char digit)
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F')
        return digit - 'A' + 10;
    return -1;
}
bool Decode(Buffer const& input, Buffer& output)
{
    output.clear();
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (input[i] == '%')
        {
            if (i + 2 >= input.size())
                return false;
            int high = HexDigit(input[i + 1]), low = HexDigit(input[i + 2]);
            if (high < 0 || low < 0 || (high == 0 && low == 0))
                return false;
            output += char(high * 16 + low);
            i += 2;
        }
        else
            output += input[i] == '+' ? ' ' : input[i];
    }
    return true;
}
}  // namespace

UrlParser::UrlParser(Buffer const& url)
{
    SetUrl(url);
}
int UrlParser::Parser()
{
    m_protocol.clear();
    m_host.clear();
    m_uri.clear();
    m_values.clear();
    http_parser_url parsed{};
    if (http_parser_parse_url(m_url.data(), m_url.size(), 0, &parsed) != 0)
        return -1;
    auto part = [&](int field) -> Buffer {
        if (!(parsed.field_set & (1 << field)))
            return {};
        return Buffer(m_url.data() + parsed.field_data[field].off, parsed.field_data[field].len);
    };
    m_protocol = part(UF_SCHEMA);
    m_host = part(UF_HOST);
    if (m_protocol.empty() || m_host.empty())
        return -2;
    m_port = (parsed.field_set & (1 << UF_PORT)) ? parsed.port : (m_protocol == "https" ? 443 : 80);
    Buffer uri = part(UF_PATH);
    if (!uri.empty() && uri.front() == '/')
        uri.erase(0, 1);
    if (!Decode(uri, m_uri))
        return -3;
    Buffer query = part(UF_QUERY);
    for (size_t start = 0; start < query.size();)
    {
        size_t end = query.find('&', start);
        if (end == std::string::npos)
            end = query.size();
        size_t equal = query.find('=', start);
        if (equal == std::string::npos || equal > end)
            return -4;
        Buffer key, value;
        if (!Decode(query.substr(start, equal - start), key) ||
            !Decode(query.substr(equal + 1, end - equal - 1), value))
            return -5;
        m_values[key] = value;
        start = end + 1;
    }
    return 0;
}
Buffer UrlParser::operator[](Buffer const& name) const
{
    auto found = m_values.find(name);
    return found == m_values.end() ? Buffer() : found->second;
}
void UrlParser::SetUrl(Buffer const& url)
{
    m_url = url;
    m_protocol.clear();
    m_host.clear();
    m_uri.clear();
    m_values.clear();
    m_port = 80;
}
