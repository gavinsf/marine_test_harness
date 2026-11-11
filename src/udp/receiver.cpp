#include "udp/receiver.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>


UdpReceiver::UdpReceiver(std::uint16_t port, int timeout_ms) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) throw std::runtime_error("socket() failed");

    int yes = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    if (timeout_ms > 0) {
        timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) < 0) {
        ::close(fd_);
        throw std::runtime_error("bind() failed");
    }
}

std::uint16_t UdpReceiver::port() const {
    sockaddr_in addr{};
    socklen_t len = sizeof addr;
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        throw std::runtime_error("getsockname() failed");
    }
    return ntohs(addr.sin_port);
}

UdpReceiver::~UdpReceiver() {
    ::close(fd_);
}

std::optional<std::string> UdpReceiver::receive() {
    char buf[2048];  // NMEA sentences are at most 82 chars, this is plenty
    const ssize_t n = ::recvfrom(fd_, buf, sizeof buf, 0, nullptr, nullptr);
    if (n >= 0) return std::string(buf, static_cast<std::size_t>(n));

    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return std::nullopt;
    throw std::runtime_error("recvfrom() failed");
}
