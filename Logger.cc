#include "Logger.h"

#include <iostream>

Logger& Logger::GetInst(){
    static Logger logger;
    return logger;
}

void Logger::setLogLevel(int level){
    logLevel_ = level;
}

// [级别信息] time: msg
void Logger::log(std::string msg){
    switch (logLevel_)
    {
    case INFO:
        std::cout << "[INFO]";
        break;
    case ERROR:
        std::cout << "[ERROR]";
        break;
    case FATAL:
        std::cout << "[FATAL]";
        break;
    case DEBUG:
        std::cout << "[DEBUG]";
        break;
    default:
        break;
    }

    // TODO...
}