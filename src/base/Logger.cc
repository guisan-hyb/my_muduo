#include <my_muduo/base/Logger.h>
#include <my_muduo/base/Timestamp.h>

#include <iostream>

Logger& Logger::GetInst(){
    static Logger logger;
    return logger;
}

void Logger::log(LogLevel level, std::string_view message){
    std::lock_guard<std::mutex> lock(mutex_);

    const char *levelName = "UNKNOWN";
    switch (level) {
    case LogLevel::Info:
        levelName = "INFO";
        break;
    case LogLevel::Error:
        levelName = "ERROR";
        break;
    case LogLevel::Fatal:
        levelName = "FATAL";
        break;
    case LogLevel::Debug:
        levelName = "DEBUG";
        break;
    }

    std::cout << '[' << levelName << ']'
              << Timestamp::now().toString() << " : " << message << std::endl;
}

