#include "Crypto.h"
#include "Epoll.h"
#include "HttpParser.h"
#include "Logger.h"
#include "Process.h"
#include "Sqlite3Client.h"
#include "MysqlClient.h"
#include "ThreadPool.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;
#define REQUIRE(expression)                        \
    do                                             \
    {                                              \
        if (!(expression))                         \
            throw std::runtime_error(#expression); \
    } while (false)

template <typename Predicate>
void Eventually(Predicate predicate)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!predicate())
    {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void TestHttp()
{
    REQUIRE(Crypto::MD5("abc") == "900150983cd24fb0d6963f7d28e17f72");
    Buffer request =
        "POST /login?user=test HTTP/1.1\r\nHost: localhost\r\nX-Test: "
        "fragmented\r\nContent-Length: 5\r\n\r\nhello";
    CHttpParser parser;
    for (char byte : request)
    {
        REQUIRE(parser.Parser(Buffer(&byte, 1)) == 1);
        REQUIRE(parser.Errno() == 0);
    }
    REQUIRE(parser.Complete());
    REQUIRE(parser.Url() == "/login?user=test");
    REQUIRE(parser.Body() == "hello");
    REQUIRE(parser.Headers().at("X-Test") == "fragmented");
    CHttpParser copy = parser;
    REQUIRE(copy.Headers() == parser.Headers());
    CHttpParser partial;
    partial.Parser("GET / HTTP/1.1\r\nHost: local");
    CHttpParser continuation = partial;
    continuation.Parser("host\r\n\r\n");
    REQUIRE(continuation.Complete() && continuation.Headers().at("Host") == "localhost");
    UrlParser url("https://example.com:8443/login?user=hello%20world&salt=a%2Bb");
    REQUIRE(url.Parser() == 0 && url.Port() == 8443 && url.Uri() == "login");
    REQUIRE(url["user"] == "hello world" && url["salt"] == "a+b");
    url.SetUrl("https://example.com/login?user=%GG");
    REQUIRE(url.Parser() != 0);
    url.SetUrl("https://example.com/other");
    REQUIRE(url.Parser() == 0 && url.Port() == 443 && url["user"].empty());
}

int ReceiveDescriptor(CProcess* process)
{
    if (process->NotifyReady(0) != 0)
        return 1;
    int descriptor = -1;
    sockaddr_in address{};
    if (process->RecvSocket(descriptor, &address) != 0 || descriptor < 0 ||
        ntohs(address.sin_port) != 4321)
        return 2;
    char bytes[5]{};
    ssize_t count = read(descriptor, bytes, sizeof(bytes));
    close(descriptor);
    if (count != 5 || std::string(bytes, 5) != "hello")
        return 3;
    if (process->RecvFD(descriptor) != 0 || descriptor != -1)
        return 4;
    return 0;
}

void TestProcess()
{
    CProcess process;
    REQUIRE(process.SetEntryFunction(ReceiveDescriptor, &process) == 0);
    REQUIRE(process.CreateSubProcess() == 0 && process.WaitReady() == 0);
    int descriptors[2];
    REQUIRE(pipe(descriptors) == 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(4321);
    REQUIRE(process.SendSocket(descriptors[0], &address) == 0);
    close(descriptors[0]);
    REQUIRE(write(descriptors[1], "hello", 5) == 5);
    close(descriptors[1]);
    REQUIRE(process.SendFD(-1) == 0);
    REQUIRE(process.Wait() == 0);
}

void TestSocketAndEpoll()
{
    CSocket listener;
    REQUIRE(listener.Init(CSockParam("127.0.0.1", 0, SOCK_ISSERVER | SOCK_ISIP | SOCK_ISREUSE)) ==
            0);
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    REQUIRE(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
    CSocket client;
    REQUIRE(client.Init(CSockParam("127.0.0.1", ntohs(address.sin_port), SOCK_ISIP)) == 0);
    REQUIRE(client.Link() == 0);
    CSocketBase* accepted = nullptr;
    REQUIRE(listener.Link(&accepted) == 0);
    std::unique_ptr<CSocketBase> owner(accepted);
    CEpoll epoll;
    REQUIRE(epoll.Create(1) == 0 && epoll.Add(*accepted, EpollData(int(*accepted))) == 0);
    REQUIRE(client.Send("ping") == 0);
    EPEvents events;
    REQUIRE(epoll.WaitEvents(events, 1000) == 1);
    REQUIRE(events.size() >= 1 && events[0].data.fd == int(*accepted));
    Buffer data(16);
    REQUIRE(accepted->Recv(data) == 4 && data == "ping");
    REQUIRE(accepted->Send("pong") == 0 && client.Recv(data) == 4 && data == "pong");
    REQUIRE(epoll.Del(*accepted) == 0);
    client.Close();
    REQUIRE(accepted->Recv(data) < 0);
}

void TestThreads()
{
    std::atomic<int> count{0};
    CThreadPool first, second;
    REQUIRE(first.Start(3) == 0 && second.Start(2) == 0);
    for (int i = 0; i < 100; ++i)
    {
        auto increment = [&count]() -> int {
            ++count;
            return 0;
        };
        REQUIRE(first.AddTask(increment) == 0 && second.AddTask(increment) == 0);
    }
    Eventually([&] { return count == 200; });
    first.Close();
    second.Close();
    REQUIRE(first.AddTask([]() -> int { return 0; }) != 0);
    REQUIRE(first.Start(1) == 0);
    first.Close();
}

void TestSqlite()
{
    CSqlite3Client database;
    REQUIRE(database.Connect({{"host", ":memory:"}}) == 0);
    _sqlite3_table_ table;
    table.Name = "people";
    for (auto field : {new _sqlite3_field_(TYPE_INT, "id", PRIMARY_KEY, "INTEGER", "", "", ""),
                       new _sqlite3_field_(TYPE_TEXT, "name", NOT_NULL, "TEXT", "", "", "")})
    {
        PField pointer(field);
        table.FieldDefine.push_back(pointer);
        table.Fields[field->Name] = pointer;
    }
    REQUIRE(database.Exec(table.Create()) == 0);
    auto values = table.Copy();
    values->Fields.at("id")->LoadFromStr("1");
    values->Fields.at("id")->Condition = SQL_INSERT;
    values->Fields.at("name")->LoadFromStr("test");
    values->Fields.at("name")->Condition = SQL_INSERT;
    REQUIRE(database.Exec(table.Insert(*values)) == 0);
    REQUIRE(database.StartTransaction() == 0);
    REQUIRE(database.Exec("INSERT INTO people VALUES(2,'rollback')") == 0);
    REQUIRE(database.RollbackTransaction() == 0);
    values->Fields.at("id")->Condition = SQL_CONDITION;
    values->Fields.at("name")->Condition = SQL_MODIFY;
    values->Fields.at("name")->LoadFromStr("updated");
    REQUIRE(database.Exec(table.Modify(*values)) == 0);
    Result rows;
    REQUIRE(database.Exec(table.Query(), rows, table) == 0 && rows.size() == 1);
    REQUIRE(rows.front()->Fields.at("id")->Value.Integer == 1);
    REQUIRE(*rows.front()->Fields.at("name")->Value.String == "updated");
    REQUIRE(database.StartTransaction() == 0);
    REQUIRE(database.Exec(table.Delete(*values)) == 0);
    REQUIRE(database.CommitTransaction() == 0);
    rows.clear();
    REQUIRE(database.Exec(table.Query(), rows, table) == 0 && rows.empty());
    REQUIRE(database.Close() == 0);
}

void TestMysql()
{
    char const* port = getenv("EPLAYER_TEST_MYSQL_PORT");
    if (!port)
        return;
    CMysqlClient database;
    REQUIRE(database.Connect({{"host", "127.0.0.1"},
                              {"port", port},
                              {"user", "root"},
                              {"password", ""},
                              {"db", "edoyun"}}) == 0);
    _mysql_table_ table;
    table.Name = "adapter_test";
    for (auto field : {new _mysql_field_(TYPE_INT, "id", PRIMARY_KEY, "INTEGER", "", "", ""),
                       new _mysql_field_(TYPE_TEXT, "name", NOT_NULL, "TEXT", "", "", "")})
    {
        PField pointer(field);
        table.FieldDefine.push_back(pointer);
        table.Fields[field->Name] = pointer;
    }
    REQUIRE(database.Exec(table.Create()) == 0);
    auto values = table.Copy();
    values->Fields.at("id")->Value.Integer = 1;
    values->Fields.at("id")->Condition = SQL_INSERT;
    values->Fields.at("name")->LoadFromStr("O'Reilly\\测试");
    values->Fields.at("name")->Condition = SQL_INSERT;
    REQUIRE(database.Exec(table.Insert(*values)) == 0);
    REQUIRE(database.StartTransaction() == 0);
    REQUIRE(database.Exec("INSERT INTO adapter_test VALUES (2,'rollback')") == 0);
    REQUIRE(database.RollbackTransaction() == 0);
    Result rows;
    REQUIRE(database.Exec(table.Query(), rows, table) == 0 && rows.size() == 1);
    REQUIRE(*rows.front()->Fields.at("name")->Value.String == "O'Reilly\\测试");
    values->Fields.at("id")->Condition = SQL_CONDITION;
    values->Fields.at("name")->Condition = SQL_MODIFY;
    values->Fields.at("name")->LoadFromStr("updated");
    REQUIRE(database.StartTransaction() == 0);
    REQUIRE(database.Exec(table.Modify(*values)) == 0);
    REQUIRE(database.CommitTransaction() == 0);
    rows.clear();
    REQUIRE(database.Exec(table.Query(), rows, table) == 0 && rows.size() == 1);
    REQUIRE(*rows.front()->Fields.at("name")->Value.String == "updated");
    REQUIRE(database.Exec(table.Delete(*values)) == 0);
    REQUIRE(database.Exec(table.Drop()) == 0);
}

void TestLogger()
{
    CLoggerServer logger;
    REQUIRE(logger.Start() == 0);
    CLoggerServer duplicate;
    REQUIRE(duplicate.Start() != 0);
    duplicate.Close();
    REQUIRE(fs::exists("log/server.sock"));
    TRACEI("format test %d %s", 42, "ok");
    LOGI << "stream test";
    unsigned char bytes[]{0, 65, 255};
    DUMPI(bytes, sizeof(bytes));
    Eventually([] {
        for (auto const& entry : fs::directory_iterator("log"))
        {
            if (entry.path().extension() != ".log")
                continue;
            std::ifstream input(entry.path());
            std::string content((std::istreambuf_iterator<char>(input)), {});
            if (content.find("format test 42 ok") != std::string::npos &&
                content.find("stream test") != std::string::npos &&
                content.find("ff") != std::string::npos)
                return true;
        }
        return false;
    });
    logger.Close();
    REQUIRE(!fs::exists("log/server.sock"));
}

int main()
{
    fs::path previous = fs::current_path();
    char pattern[] = "/tmp/eplayer-components-XXXXXX";
    char* directory = mkdtemp(pattern);
    if (!directory)
        return 1;
    fs::current_path(directory);
    int status = 0;
    try
    {
        TestHttp();
        TestProcess();
        TestSocketAndEpoll();
        TestThreads();
        TestSqlite();
        TestLogger();
        TestMysql();
        std::cout
            << "HTTP, crypto, processes, sockets, epoll, thread pools, SQLite and logger passed\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << "Test failed: " << error.what() << '\n';
        for (auto const& entry : fs::directory_iterator(fs::exists("log") ? "log" : "."))
        {
            if (entry.path().extension() == ".log")
            {
                std::ifstream input(entry.path());
                std::cerr << input.rdbuf();
            }
        }
        status = 1;
    }
    fs::current_path(previous);
    fs::remove_all(directory);
    return status;
}
