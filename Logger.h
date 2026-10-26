#pragma once

#include <string>

#include "noncopyable.h"


// LOG_INFO("%s %d", arg1, arg2)
#define LOG_INFO(logmsgFormat, ...)                     \
    do                                                  \
    {                                                   \
        Logger &logger = Logger::GetInst();             \
        logger.setLogLevel(INFO);                       \
        char buf[1024] = {0};                           \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS__); \
        logger.log(buf);                                \
    } while (0)

#define LOG_ERROR(logmsgFormat, ...)                     \
    do                                                  \
    {                                                   \
        Logger &logger = Logger::GetInst();             \
        logger.setLogLevel(ERROR);                       \
        char buf[1024] = {0};                           \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS); \
        logger.log(buf);                                \
    } while (0)

#define LOG_FATAL(logmsgFormat, ...)                     \
    do                                                  \
    {                                                   \
        Logger &logger = Logger::GetInst();             \
        logger.setLogLevel(FATAL);                       \
        char buf[1024] = {0};                           \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS); \
        logger.log(buf);                                \
    } while (0)

#ifdef MUDEBUG
#define LOG_DEBUG(logmsgFormat, ...)                     \
    do                                                  \
    {                                                   \
        Logger &logger = Logger::GetInst();             \
        logger.setLogLevel(DEBUG);                       \
        char buf[1024] = {0};                           \
        snprintf(buf, 1024, logmsgFormat, ##__VA_ARGS); \
        logger.log(buf);                                \
    } while (0)
#else
    #define LOG_DEBUG(logmsgFormat, ...)
#endif



// 定义日志级别 INFO ERROR FATAL DEBUG
enum LogLevel
{
    INFO,  // 普通信息
    ERROR, // 错误信息
    FATAL, // core信息
    DEBUG, // 调试信息
};


// 输出一个日志类
class Logger : public noncopyable
{
public:
    static Logger& GetInst();
    void setLogLevel(int level);// 设置日志级别
    void log(std::string msg);

private:
    Logger() = default;

    int logLevel_;
};