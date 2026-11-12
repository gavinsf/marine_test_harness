#include <gtest/gtest.h>
#include <string>
#include "common/checksum.hpp"

TEST(Checksum, SameInputGivesSameOutput) {
    EXPECT_EQ(checksum("1"), checksum("1"));
}
TEST(Checksum, IsTheXorOfEveryByte) {
    EXPECT_EQ(checksum(""), 0);
    EXPECT_EQ(checksum("A"), 0x41);
    EXPECT_EQ(checksum("AB"), 0x03);
    EXPECT_EQ(checksum("GPGGA"), 0x56);
    // Published GGA example.
    EXPECT_EQ(checksum("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"), 0x47);
}

TEST(Checksum, DetectsEverySingleBitFlip) {
    const std::string s = "HEHDT,274.07,T";
    const auto good = checksum(s);
    for (std::size_t i = 0; i < s.size(); ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            std::string t = s;
            t[i] = static_cast<char>(t[i] ^ (1 << bit));
            EXPECT_NE(checksum(t), good) << "byte " << i << " bit " << bit;
        }
    }
}

TEST(HexDigit, DecodesBothCases) {
    EXPECT_EQ(hex_digit('0'), 0);
    EXPECT_EQ(hex_digit('9'), 9);
    EXPECT_EQ(hex_digit('a'), 10);
    EXPECT_EQ(hex_digit('F'), 15);
    EXPECT_EQ(hex_digit('G'), -1);
    EXPECT_EQ(hex_digit(' '), -1);
    EXPECT_EQ(hex_digit('\0'), -1);
}
