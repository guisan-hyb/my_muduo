#include <my_muduo/base/Timestamp.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>

Timestamp::Timestamp() : microSecondsSinceEpoch_(0) {

}

Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
    : microSecondsSinceEpoch_(microSecondsSinceEpoch)
{}

Timestamp Timestamp::now(){
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return Timestamp(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

std::string Timestamp::toString() const {
    const auto secondsPart = microSecondsSinceEpoch_ / 1000000;
    const auto microsPart = microSecondsSinceEpoch_ % 1000000;
    const auto seconds = static_cast<std::time_t>(secondsPart);

    std::tm localTime{};
    if (::localtime_r(&seconds, &localTime) == nullptr) {
        return {};
    }

    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(),
                  "%04d/%02d/%02d %02d:%02d:%02d.%06lld",
                  localTime.tm_year + 1900,
                  localTime.tm_mon + 1,
                  localTime.tm_mday,
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec,
                  static_cast<long long>(microsPart));

    return buffer.data();
}


// #include <iostream>
// int main(){
//     std::cout << Timestamp::now().toString() << std::endl;

//     return 0;
// }
