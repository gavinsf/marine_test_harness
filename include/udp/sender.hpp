#pragma once
#include <netinet/in.h> 

#include <cstdint>
#include <string>
#include <string_view>

class UdpSender {
    public:
        UdpSender(const std::string& host, std::uint16_t port);
        ~UdpSender();
        UdpSender(const UdpSender&) = delete;
        UdpSender& operator=(const UdpSender&) = delete;

        void operator()(std::string_view msg) const;
    private:
        int fd_ = -1;
        sockaddr_in dest_{};
};