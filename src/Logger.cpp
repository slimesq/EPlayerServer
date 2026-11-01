#include "Logger.h"
#include <cstdarg>
#include <iomanip>
#include <sys/time.h>
#include <vector>

namespace
{
Buffer Prefix(char const* file, int line, char const* function, pid_t pid, pthread_t tid, int level)
{
    char const* levels[]{"INFO", "DEBUG", "WARNING", "ERROR", "FATAL"};
    std::ostringstream out;
    out << file << '(' << line << "):[" << levels[(level >= 0 && level <= 4) ? level : LOG_ERROR]
        << "][" << CLoggerServer::GetTimeStr() << "]<" << pid << '-' << tid << ">(" << function
        << ") ";
    return out.str();
} 
}  // namespace

LogInfo::LogInfo(char const* file,
                 int line,
                 char const* function,
                 pid_t pid,
                 pthread_t tid,
                 int level,
                 char const* format,
                 ...)
    : m_buf(Prefix(file, line, function, pid, tid, level))
{
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int size = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (size >= 0)
    {
        std::vector<char> text(static_cast<size_t>(size) + 1);
        vsnprintf(text.data(), text.size(), format, arguments);
        m_buf.append(text.data(), static_cast<size_t>(size));
    }
    va_end(arguments);
    m_buf += '\n';
}

LogInfo::LogInfo(
    char const* file, int line, char const* function, pid_t pid, pthread_t tid, int level)
    : bAuto(true), m_buf(Prefix(file, line, function, pid, tid, level))
{
}

LogInfo::LogInfo(char const* file,
                 int line,
                 char const* function,
                 pid_t pid,
                 pthread_t tid,
                 int level,
                 LogData dump)
    : m_buf(Prefix(file, line, function, pid, tid, level) + "\n")
{
    auto* bytes = static_cast<unsigned char const*>(dump.data);
    size_t size = dump.size;
    if (!bytes && size)
        return;
    for (size_t offset = 0; offset < size; offset += 16)
    {
        std::ostringstream out;
        for (size_t i = 0; i < 16; ++i)
        {
            if (offset + i < size)
                out << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[offset + i])
                    << ' ';
            else
                out << "   ";
        }
        out << "; ";
        for (size_t i = offset; i < size && i < offset + 16; ++i)
            out << ((bytes[i] >= 32 && bytes[i] < 127) ? char(bytes[i]) : '.');
        m_buf += out.str() + '\n';
    }
}

LogInfo::~LogInfo()
{
    if (bAuto)
    {
        m_buf += '\n';
        CLoggerServer::Trace(*this);
    }
}

Buffer CLoggerServer::GetTimeStr()
{
    timeval now{};
    gettimeofday(&now, nullptr);
    tm time{};
    localtime_r(&now.tv_sec, &time);
    char text[64]{};
    strftime(text, sizeof(text), "%Y-%m-%d_%H-%M-%S", &time);
    char suffix[32]{};
    snprintf(suffix, sizeof(suffix), "_%03ld_%ld", now.tv_usec / 1000, long(getpid()));
    return std::string(text) + suffix;
}
