
#include "Process.h"

#include <iostream>
#include <sys/fcntl.h>

int CreateLogServer(CProcess* proc)
{
    std::cout << __FILE__ << "(" << __LINE__ << ")"
              << ":<" << __FUNCTION__ << "> pid=" << getpid() << std::endl;

    return 0;
}

int CreateClientServer(CProcess* proc)
{
    // sleep(1);
    int fd{-1};
    int ret = proc->RecvFd(fd);
    std::cout << __FILE__ << "(" << __LINE__ << ")"
              << ":<" << __FUNCTION__ << "> pid=" << getpid() << " ret = " << ret << " fd = " << fd
              << std::endl;
    char buf[10] = "";
    lseek(fd, 0, SEEK_SET);
    read(fd, buf, sizeof(buf));
    std::cout << "buf = \"" << buf << "\"" << std::endl;
    close(fd);
    return 0;
}

int main()
{
    // CProcess::SwitchDeamon();
    CProcess proclog, procclients;
    proclog.SetEntryFunction(CreateLogServer, &proclog);
    int ret = proclog.CreateSubProcess();
    if (ret == -1)
    {
        std::cout << __FILE__ << "(" << __LINE__ << ")"
                  << ":<" << __FUNCTION__ << "> pid=" << getpid() << std::endl;
        return -1;
    }
    procclients.SetEntryFunction(CreateClientServer, &procclients);
    ret = procclients.CreateSubProcess();
    if (ret != 0)
    {
        std::cout << __FILE__ << "(" << __LINE__ << ")"
                  << ":<" << __FUNCTION__ << "> pid=" << getpid() << std::endl;
        return -2;
    }

    int fd = open("./text.txt", O_RDWR | O_CREAT, 0644);
    if (fd == -1)
    {
        std::cout << __FILE__ << "(" << __LINE__ << ")"
                  << ":<" << __FUNCTION__ << ">" << std::endl;
        return -3;
    }
    write(fd, "edoyun", 6);
    ret = procclients.SendFD(fd);
    std::cout << __FILE__ << "(" << __LINE__ << ")"
              << ":<" << __FUNCTION__ << "> pid=" << getpid() << " fd = " << fd << std::endl;
    close(fd);

    return 0;
}
