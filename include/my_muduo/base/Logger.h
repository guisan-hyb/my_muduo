#pragma once

#include <array>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string_view>

#include <my_muduo/base/noncopyable.h>

enum class LogLevel
{
    Info,
    Error,
    Fatal,
    Debug,
};

// LOG_INFO("%s %d", arg1, arg2)
#define LOG_INFO(logmsgFormat, ...)                     \
    do                                                  \
    {                                                   \
        Logger &logger = Logger::GetInst();             \
        std::array<char, 1024> myMuduoLogBuffer{};      \
        std::snprintf(myMuduoLogBuffer.data(), myMuduoLogBuffer.size(), \
                      logmsgFormat, ##__VA_ARGS__);    \
        logger.log(LogLevel::Info, myMuduoLogBuffer.data()); \
    } while (0)

#define LOG_ERROR(logmsgFormat, ...)                     \
    do                                                  \
    {                                                   \
        Logger &logger = Logger::GetInst();             \
        std::array<char, 1024> myMuduoLogBuffer{};      \
        std::snprintf(myMuduoLogBuffer.data(), myMuduoLogBuffer.size(), \
                      logmsgFormat, ##__VA_ARGS__);    \
        logger.log(LogLevel::Error, myMuduoLogBuffer.data()); \
    } while (0)

#define LOG_FATAL(logmsgFormat, ...)                      \
    do                                                    \
    {                                                     \
        Logger &logger = Logger::GetInst();               \
        std::array<char, 1024> myMuduoLogBuffer{};        \
        std::snprintf(myMuduoLogBuffer.data(), myMuduoLogBuffer.size(), \
                      logmsgFormat, ##__VA_ARGS__);      \
        logger.log(LogLevel::Fatal, myMuduoLogBuffer.data()); \
        std::exit(EXIT_FAILURE);                          \
    } while (0)

#ifdef MUDEBUG
#define LOG_DEBUG(logmsgFormat, ...)                     \
    do                                                  \
    {                                                   \
        Logger &logger = Logger::GetInst();             \
        std::array<char, 1024> myMuduoLogBuffer{};      \
        std::snprintf(myMuduoLogBuffer.data(), myMuduoLogBuffer.size(), \
                      logmsgFormat, ##__VA_ARGS__);    \
        logger.log(LogLevel::Debug, myMuduoLogBuffer.data()); \
    } while (0)
#else
    #define LOG_DEBUG(logmsgFormat, ...)
#endif



// 输出一个日志类
class Logger : public noncopyable
{
public:
    static Logger& GetInst();
    void log(LogLevel level, std::string_view message);

private:
    Logger() = default;
    std::mutex mutex_;
};
