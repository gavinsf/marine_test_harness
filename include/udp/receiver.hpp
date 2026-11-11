#pragma once
#include <cstdint>
#include <optional>
#include <string>

class UdpReceiver {
    public:
        explicit UdpReceiver(std::uint16_t port , int timeout_ms = 1000);
        ~UdpReceiver();
        UdpReceiver(const UdpReceiver&) = delete;
        UdpReceiver& operator=(const UdpReceiver&) = delete;

        std::optional<std::string> receive();

        // Port the socket is actually bound to (useful when constructed with port 0).
        std::uint16_t port() const;

    private:
        int fd_ = -1;
};