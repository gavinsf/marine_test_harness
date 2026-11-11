#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "udp/receiver.hpp"
#include "udp/sender.hpp"

namespace {

constexpr int kArriveTimeoutMs = 2000;
constexpr const char* kLoopback = "127.0.0.1";

class UdpSenderTest : public ::testing::Test {
protected:
    // Port 0 => kernel-assigned, so tests are isolated and parallel-safe.
    UdpReceiver rx{0, kArriveTimeoutMs};
    UdpSender tx{kLoopback, rx.port()};
};

}  // namespace

// ---- construction ---------------------------------------------------------

TEST(UdpSenderConstruction, AcceptsValidIpv4Address) {
    EXPECT_NO_THROW(UdpSender(kLoopback, 9999));
}

TEST(UdpSenderConstruction, RejectsMalformedAddresses) {
    for (const char* bad : {"", "not-an-ip", "256.0.0.1", "1.2.3", "1.2.3.4.5", "::1"}) {
        EXPECT_THROW(UdpSender(bad, 9999), std::invalid_argument) << "host: '" << bad << "'";
    }
}

TEST(UdpSenderConstruction, DoesNotResolveHostnames) {
    // Only dotted-quad IPv4 is supported (inet_pton); document that explicitly.
    EXPECT_THROW(UdpSender("localhost", 9999), std::invalid_argument);
}

TEST(UdpSenderConstruction, ErrorMessageNamesTheBadAddress) {
    try {
        UdpSender("bogus", 9999);
        FAIL() << "expected std::invalid_argument";
    } catch (const std::invalid_argument& e) {
        EXPECT_NE(std::string(e.what()).find("bogus"), std::string::npos);
    }
}

TEST(UdpSenderConstruction, DoesNotLeakDescriptorsWhenAddressIsInvalid) {
    // Repeated failed construction would exhaust the fd table if the error
    // path forgot to close the socket (default limit is typically 1024).
    for (int i = 0; i < 2000; ++i) {
        EXPECT_THROW(UdpSender("bad", 1), std::invalid_argument);
    }
    // A fresh socket can still be created afterwards.
    EXPECT_NO_THROW(UdpSender(kLoopback, 1));
}

// ---- sending --------------------------------------------------------------

TEST_F(UdpSenderTest, DeliversMessageToReceiver) {
    tx("$GPGGA,123519*47");

    EXPECT_EQ(rx.receive(), "$GPGGA,123519*47");
}

TEST_F(UdpSenderTest, SendsEachCallAsSeparateDatagram) {
    tx("one");
    tx("two");
    tx("three");

    EXPECT_EQ(rx.receive(), "one");
    EXPECT_EQ(rx.receive(), "two");
    EXPECT_EQ(rx.receive(), "three");
}

TEST_F(UdpSenderTest, SendsEmptyMessageAsEmptyDatagram) {
    tx(std::string_view{});

    const auto msg = rx.receive();

    ASSERT_TRUE(msg.has_value());
    EXPECT_TRUE(msg->empty());
}

TEST_F(UdpSenderTest, PreservesEmbeddedNulBytes) {
    const std::string payload("ab\0cd", 5);
    tx(std::string_view(payload));

    EXPECT_EQ(rx.receive(), payload);
}

TEST_F(UdpSenderTest, SendsSubstringViewWithoutNeedingNulTerminator) {
    const std::string full = "HELLO-WORLD";
    tx(std::string_view(full).substr(0, 5));

    EXPECT_EQ(rx.receive(), "HELLO");
}

TEST_F(UdpSenderTest, CanBeInvokedThroughConstReference) {
    const UdpSender& ctx = tx;
    ctx("const-ok");

    EXPECT_EQ(rx.receive(), "const-ok");
}

TEST_F(UdpSenderTest, IsUsableAsStdFunctionSink) {
    // The simulator consumes senders as generic callables.
    std::function<void(std::string_view)> sink = std::cref(tx);
    sink("via-function");

    EXPECT_EQ(rx.receive(), "via-function");
}

TEST_F(UdpSenderTest, TwoSendersCanTargetDifferentReceivers) {
    UdpReceiver rx2{0, kArriveTimeoutMs};
    UdpSender tx2{kLoopback, rx2.port()};

    tx("to-first");
    tx2("to-second");

    EXPECT_EQ(rx.receive(), "to-first");
    EXPECT_EQ(rx2.receive(), "to-second");
}

TEST(UdpSenderNoListener, SendingToClosedPortDoesNotThrowOrCrash) {
    // UDP is fire-and-forget: with nobody listening, sending must not throw.
    std::uint16_t dead_port = 0;
    {
        UdpReceiver temp{0, 50};
        dead_port = temp.port();
    }  // closed here
    UdpSender tx(kLoopback, dead_port);

    EXPECT_NO_THROW(tx("nobody-home"));
    EXPECT_NO_THROW(tx("still-nobody"));  // a prior ICMP error must not poison later sends
}
