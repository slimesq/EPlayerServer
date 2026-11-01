#include "EdoyunPlayerServer.h"
#include <iostream>
#include <stdexcept>

namespace
{
CServer* activeServer = nullptr;

void StopServer(int)
{
    if (activeServer)
        activeServer->RequestStop();
}

int LogProcess(CProcess* process)
{
    CLoggerServer logger;
    int result = logger.Start();
    process->NotifyReady(result);
    if (result != 0)
        return result;
    int descriptor = -1;
    process->RecvFD(descriptor);
    if (descriptor >= 0)
        close(descriptor);
    logger.Close();
    return 0;
}

unsigned Number(char const* value, unsigned maximum)
{
    size_t end = 0;
    unsigned long number = std::stoul(value, &end);
    if (value[end] != '\0' || number == 0 || number > maximum)
        throw std::invalid_argument("number out of range");
    return static_cast<unsigned>(number);
}
}  // namespace

int main(int argc, char** argv)
{
    Buffer host = "0.0.0.0";
    unsigned port = 9999, workers = 4;
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string option = argv[i];
            if (option == "--help")
            {
                std::cout
                    << "Usage: EPlayerServer [--host IPv4] [--port 1..65535] [--workers 1..128]\n"
                       "MySQL environment: EPLAYER_DB_HOST (127.0.0.1), EPLAYER_DB_PORT (3306),\n"
                       "  EPLAYER_DB_USER (root), EPLAYER_DB_PASSWORD, EPLAYER_DB_NAME (edoyun).\n";
                return 0;
            }
            if (i + 1 >= argc)
                throw std::invalid_argument("missing option value");
            char const* value = argv[++i];
            if (option == "--host")
                host = value;
            else if (option == "--port")
                port = Number(value, 65535);
            else if (option == "--workers")
                workers = Number(value, 128);
            else
                throw std::invalid_argument("unknown option");
        }
        in_addr address{};
        if (inet_pton(AF_INET, host, &address) != 1)
            throw std::invalid_argument("invalid IPv4 address");
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << "; use --help for usage.\n";
        return 2;
    }

    CProcess logProcess;
    logProcess.SetEntryFunction(LogProcess, &logProcess);
    if (logProcess.CreateSubProcess() != 0 || logProcess.WaitReady() != 0)
    {
        std::cerr
            << "Cannot start logger; check log directory permissions and existing server.sock.\n";
        logProcess.SendFD(-1);
        logProcess.Wait();
        return 1;
    }
    CEdoyunPlayerServer business(workers);
    CServer server;
    int result = server.Init(&business, host, static_cast<short>(port));
    if (result == 0)
    {
        activeServer = &server;
        struct sigaction action{};
        action.sa_handler = StopServer;
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
        std::cout << "EPlayerServer listening on " << host.c_str() << ':' << port << std::endl;
        server.Run();
        activeServer = nullptr;
    }
    else
        std::cerr << "Server startup failed (" << result
                  << "). Check MySQL configuration and listen port.\n";
    server.Close();
    logProcess.SendFD(-1);
    logProcess.Wait();
    return result == 0 ? 0 : 1;
}
