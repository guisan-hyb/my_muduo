#include <my_muduo/net/InetAddress.h>

#include <array>
#include <cstring>

InetAddress::InetAddress(uint16_t port, const std::string &ip)
    : addr_{}
{
    addr_.sin_family = AF_INET;
    addr_.sin_addr.s_addr = ::inet_addr(ip.c_str());
    addr_.sin_port = htons(port);
}

std::string InetAddress::toIp() const{
    std::array<char, INET_ADDRSTRLEN> buffer{};
    ::inet_ntop(AF_INET, &addr_.sin_addr, buffer.data(), buffer.size());
    return buffer.data();
}

std::string InetAddress::toIpPort() const{
    return toIp() + ":" + std::to_string(toPort());
}

uint16_t InetAddress::toPort() const{
    return ntohs(addr_.sin_port);
}



// #include <iostream>
// int main(){
//     InetAddress addr(8080);
//     std::cout << addr.toIpPort() << std::endl;

//     return 0;
// }

