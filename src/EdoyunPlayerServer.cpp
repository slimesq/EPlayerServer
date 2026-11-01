#include "EdoyunPlayerServer.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <openssl/crypto.h>

namespace
{
Buffer Environment(char const* name, char const* fallback)
{
    char const* value = getenv(name);
    return value ? value : fallback;
}
}  // namespace

CEdoyunPlayerServer::~CEdoyunPlayerServer()
{
    m_stopping = true;
    m_pool.Close();
    m_epoll.Close();
    m_mapClients.clear();
    if (m_db)
        m_db->Close();
}

int CEdoyunPlayerServer::BusinessProcess(CProcess* process)
{
    using namespace std::placeholders;
    m_db = std::make_unique<CMysqlClient>();
    KeyValue args;
    args["host"] = Environment("EPLAYER_DB_HOST", "127.0.0.1");
    args["port"] = Environment("EPLAYER_DB_PORT", "3306");
    args["user"] = Environment("EPLAYER_DB_USER", "root");
    args["password"] = Environment("EPLAYER_DB_PASSWORD", "");
    args["db"] = Environment("EPLAYER_DB_NAME", "edoyun");
    int result = m_db->Connect(args);
    if (result != 0)
    {
        fprintf(stderr,
                "MySQL connection failed; configure EPLAYER_DB_HOST/PORT/USER/PASSWORD/NAME.\n");
        return -1;
    }
    edoyunLogin_user_mysql table;
    if (m_db->Exec(table.Create()) != 0)
        return -2;
    setConnectedCallback(&CEdoyunPlayerServer::Connected, this, _1);
    setRecvCallback(&CEdoyunPlayerServer::Received, this, _1, _2);
    if (m_epoll.Create(m_count) != 0 || m_pool.Start(m_count) != 0)
        return -3;
    for (unsigned i = 0; i < m_count; ++i)
        if (m_pool.AddTask(&CEdoyunPlayerServer::ThreadFunc, this) != 0)
            return -4;
    process->NotifyReady(0);
    for (;;)
    {
        int descriptor = -1;
        sockaddr_in address{};
        if (process->RecvSocket(descriptor, &address) != 0 || descriptor < 0)
            break;
        auto state = std::make_shared<ClientState>();
        state->socket.reset(new CSocket(descriptor));
        if (state->socket->Init(CSockParam(&address, SOCK_ISIP | SOCK_ISNONBLOCK)) != 0)
            continue;
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            m_mapClients[descriptor] = state;
        }
        if (m_connectedcallback)
            (*m_connectedcallback)(state->socket.get());
        if (m_epoll.Add(descriptor, EpollData(descriptor), EPOLLIN | EPOLLRDHUP | EPOLLONESHOT) !=
            0)
            RemoveClient(descriptor);
    }
    m_stopping = true;
    m_pool.Close();
    m_epoll.Close();
    m_mapClients.clear();
    m_db->Close();
    return 0;
}

int CEdoyunPlayerServer::Connected(CSocketBase* client)
{
    sockaddr_in const* address = *client;
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip));
    TRACEI("client connected: %s:%u", ip, unsigned(ntohs(address->sin_port)));
    return 0;
}

int CEdoyunPlayerServer::Received(CSocketBase* client, Buffer const& data)
{
    return client->Send(MakeResponse(HttpParser(data)));
}

int CEdoyunPlayerServer::HttpParser(Buffer const& data)
{
    CHttpParser parser;
    parser.Parser(data);
    if (parser.Errno() || !parser.Complete())
        return -1;
    if (parser.Method() != HTTP_GET)
        return -7;
    UrlParser url("http://127.0.0.1" + parser.Url());
    if (url.Parser() != 0)
        return -2;
    if (url.Uri() != "login")
        return -7;
    Buffer timestamp = url["time"], salt = url["salt"], user = url["user"], sign = url["sign"];
    if (timestamp.empty() || salt.empty() || user.empty() || sign.size() != 32)
        return -6;
    edoyunLogin_user_mysql table;
    Result rows;
    {
        std::lock_guard<std::mutex> lock(m_databaseMutex);
        Buffer condition = "user_name='" + m_db->Escape(user) + "'";
        if (m_db->Exec(table.Query(condition), rows, table) != 0)
            return -3;
    }
    if (rows.empty())
        return -4;
    if (rows.size() != 1)
        return -5;
    Buffer password = *rows.front()->Fields.at("user_password")->Value.String;
    Buffer key = Environment("EPLAYER_SIGN_KEY", "*&^%$#@b.v+h-b*g/h@n!h#n$d^ssx,.kl<kl");
    Buffer expected = Crypto::MD5(timestamp + key + password + salt);
    std::transform(sign.begin(), sign.end(), sign.begin(), [](unsigned char value) {
        return char(std::tolower(value));
    });
    return CRYPTO_memcmp(expected.data(), sign.data(), expected.size()) == 0 ? 0 : -6;
}

Buffer CEdoyunPlayerServer::MakeResponse(int result)
{
    Json::Value body;
    body["status"] = result;
    body["message"] = result == 0 ? "success" : "登录失败，可能是用户名或者密码错误！";
    Buffer json = body.toStyledString();
    time_t now = time(nullptr);
    tm utc{};
    gmtime_r(&now, &utc);
    char date[80]{};
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", &utc);
    Buffer response = "HTTP/1.1 200 OK\r\nDate: ";
    response += date;
    response +=
        "\r\nServer: Edoyun/1.0\r\nContent-Type: application/json; charset=utf-8\r\nConnection: "
        "close\r\nContent-Length: ";
    response += std::to_string(json.size());
    response += "\r\nX-Content-Type-Options: nosniff\r\n\r\n";
    return response + json;
}

void CEdoyunPlayerServer::RemoveClient(int descriptor)
{
    m_epoll.Del(descriptor);
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    m_mapClients.erase(descriptor);
}

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
