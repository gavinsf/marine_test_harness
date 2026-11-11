#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "udp/receiver.hpp"

namespace {

using namespace std::chrono_literals;

// Generous timeout for "data should arrive" paths: it only costs time when the
// test is already failing, and avoids flakes on loaded CI machines.
constexpr int kArriveTimeoutMs = 2000;

// Raw-socket sender so the receiver is tested independently of UdpSender.
class RawSender {
public:
    RawSender() : fd_(::socket(AF_INET, SOCK_DGRAM, 0)) { EXPECT_GE(fd_, 0); }
    ~RawSender() { if (fd_ >= 0) ::close(fd_); }
    RawSender(const RawSender&) = delete;
    RawSender& operator=(const RawSender&) = delete;

    void send_to(std::uint16_t port, const std::string& payload) const {
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(port);
        ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr), 1);
        const ssize_t n = ::sendto(fd_, payload.data(), payload.size(), 0,
                                   reinterpret_cast<const sockaddr*>(&dst), sizeof dst);
        ASSERT_EQ(n, static_cast<ssize_t>(payload.size()));
    }

private:
    int fd_;
};

class UdpReceiverTest : public ::testing::Test {
protected:
    // Port 0 => kernel picks a free port, so tests never collide with each
    // other, parallel ctest runs, or other services on the machine.
    UdpReceiver rx{0, kArriveTimeoutMs};
    RawSender tx;
};

}  // namespace

TEST_F(UdpReceiverTest, BindsToEphemeralPortWhenGivenPortZero) {
    EXPECT_NE(rx.port(), 0);
}

TEST_F(UdpReceiverTest, DistinctReceiversGetDistinctEphemeralPorts) {
    UdpReceiver other{0, 50};
    EXPECT_NE(rx.port(), other.port());
}

TEST_F(UdpReceiverTest, ReceivesSingleDatagram) {
    tx.send_to(rx.port(), "$GPGGA,123519*47");

    const auto msg = rx.receive();

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(*msg, "$GPGGA,123519*47");
}

TEST_F(UdpReceiverTest, ReturnsOneDatagramPerCallPreservingBoundaries) {
    tx.send_to(rx.port(), "first");
    tx.send_to(rx.port(), "second");
    tx.send_to(rx.port(), "third");

    EXPECT_EQ(rx.receive(), "first");
    EXPECT_EQ(rx.receive(), "second");
    EXPECT_EQ(rx.receive(), "third");
}

TEST_F(UdpReceiverTest, ReceivesEmptyDatagramAsEmptyStringNotNullopt) {
    tx.send_to(rx.port(), "");

    const auto msg = rx.receive();

    ASSERT_TRUE(msg.has_value());
    EXPECT_TRUE(msg->empty());
}

TEST_F(UdpReceiverTest, PreservesEmbeddedNulBytes) {
    const std::string payload("ab\0cd\0", 6);
    tx.send_to(rx.port(), payload);

    const auto msg = rx.receive();

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->size(), 6u);
    EXPECT_EQ(*msg, payload);
}

TEST_F(UdpReceiverTest, ReceivesMaxBufferSizedDatagramIntact) {
    const std::string payload(2048, 'x');  // exactly the internal buffer size
    tx.send_to(rx.port(), payload);

    const auto msg = rx.receive();

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(*msg, payload);
}

// Characterization test: documents current behaviour for oversized datagrams
// (the kernel silently discards the excess). Update if truncation handling changes.
TEST_F(UdpReceiverTest, TruncatesDatagramsLargerThanInternalBuffer) {
    tx.send_to(rx.port(), std::string(4000, 'y'));

    const auto msg = rx.receive();

    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->size(), 2048u);
}

TEST(UdpReceiverTimeout, ReturnsNulloptWhenNothingArrives) {
    UdpReceiver rx{0, 50};

    const auto start = std::chrono::steady_clock::now();
    const auto msg = rx.receive();
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(msg.has_value());
    // Lower bound only is strict; the upper bound is deliberately loose so a
    // busy machine does not cause spurious failures.
    EXPECT_GE(elapsed, 40ms);
    EXPECT_LT(elapsed, 1500ms);
}

TEST(UdpReceiverTimeout, RemainsUsableAfterTimeout) {
    UdpReceiver rx{0, 50};
    ASSERT_FALSE(rx.receive().has_value());

    RawSender tx;
    tx.send_to(rx.port(), "late");

    EXPECT_EQ(rx.receive(), "late");
}

TEST(UdpReceiverTimeout, NonPositiveTimeoutBlocksButReturnsQueuedData) {
    // timeout_ms <= 0 means "block forever", so only exercise it with data
    // already queued; an empty socket here would hang the test.
    UdpReceiver rx{0, 0};
    RawSender tx;
    tx.send_to(rx.port(), "queued");

    EXPECT_EQ(rx.receive(), "queued");
}

TEST(UdpReceiverConstruction, ThrowsWhenBindFails) {
    // Occupy a port with a plain socket (no SO_REUSEADDR) so the receiver's
    // bind() to the same port must fail.
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(fd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    ASSERT_EQ(::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr), 0);
    socklen_t len = sizeof addr;
    ASSERT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len), 0);

    EXPECT_THROW(UdpReceiver(ntohs(addr.sin_port), 50), std::runtime_error);

    ::close(fd);
}
