#include "udp/sender.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <stdexcept>

UdpSender::UdpSender(const std::string& host, std::uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) throw std::runtime_error("socket() failed");

    dest_.sin_family = AF_INET;
    dest_.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &dest_.sin_addr) != 1) {
        ::close(fd_);
        throw std::invalid_argument("bad IPv4 address: " + host);
    }
}

UdpSender::~UdpSender() {
    ::close(fd_);
}

void UdpSender::operator()(std::string_view msg) const {
    ::sendto(fd_, msg.data(), msg.size(), 0,
            reinterpret_cast<const sockaddr*>(&dest_), sizeof dest_);
}