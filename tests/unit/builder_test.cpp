#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include <string>
#include "sim/builder.hpp"

namespace {

ShipState sampleState() {
    ShipState s{};
    s.lat = 48.1173;
    s.lon = 11.516666667;
    s.sog = 22.4;
    s.cog = 84.4;
    s.heading = 84.4;
    s.depth = 12.3;
    s.utc.tm_hour = 12;
    s.utc.tm_min = 35;
    s.utc.tm_sec = 19;
    s.utc.tm_mday = 23;
    s.utc.tm_mon = 2;      // March
    s.utc.tm_year = 94;    // 1994
    s.centis = 0;
    return s;
}

// Independent checksum check so the test does not depend on checksum().
bool hasValidChecksum(const std::string& sentence) {
    if (sentence.size() < 9 || sentence.front() != '$') return false;
    auto star = sentence.rfind('*');
    if (star == std::string::npos || sentence.size() != star + 5) return false;
    if (sentence.substr(star + 3) != "\r\n") return false;
    uint8_t cs = 0;
    for (size_t i = 1; i < star; ++i) cs ^= static_cast<uint8_t>(sentence[i]);
    auto declared = static_cast<uint8_t>(std::strtoul(sentence.substr(star + 1, 2).c_str(), nullptr, 16));
    return cs == declared;
}

std::string bodyOf(const std::string& sentence) {
    return sentence.substr(1, sentence.rfind('*') - 1);
}

}  // namespace

// REQ-001: GGA, RMC, HDT and DPT sentences carry a correct checksum.
TEST(SentenceBuilder, Req001_AllSentencesHaveCorrectChecksum) {
    ShipState s = sampleState();
    EXPECT_TRUE(hasValidChecksum(gga(s))) << gga(s);
    EXPECT_TRUE(hasValidChecksum(rmc(s))) << rmc(s);
    EXPECT_TRUE(hasValidChecksum(hdt(s))) << hdt(s);
    EXPECT_TRUE(hasValidChecksum(dpt(s))) << dpt(s);
}

TEST(SentenceBuilder, Req001_ChecksumIsUppercaseHexTwoDigits) {
    EXPECT_EQ(frame("GPGGA"), "$GPGGA*56\r\n");
    // Checksum below 0x10 must be zero padded.
    EXPECT_EQ(frame("A"), "$A*41\r\n");
    EXPECT_EQ(frame("AB"), "$AB*03\r\n");
}

TEST(SentenceBuilder, Req001_FrameMatchesKnownReferenceChecksum) {
    EXPECT_EQ(frame("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
              "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n");
}

TEST(SentenceBuilder, Req001_ChecksumValidAcrossPositions) {
    ShipState s = sampleState();
    for (double lat : {-89.9999, -0.0001, 0.0, 33.3333, 89.9999}) {
        for (double lon : {-179.9999, -0.0001, 0.0, 77.7777, 179.9999}) {
            s.lat = lat;
            s.lon = lon;
            EXPECT_TRUE(hasValidChecksum(gga(s))) << gga(s);
            EXPECT_TRUE(hasValidChecksum(rmc(s))) << rmc(s);
        }
    }
}

TEST(SentenceBuilder, Req001_GgaLayout) {
    EXPECT_EQ(bodyOf(gga(sampleState())),
              "GPGGA,123519.00,4807.0380,N,01131.0000,E,1,08,0.9,0.0,M,,M,,");
}

TEST(SentenceBuilder, Req001_RmcLayout) {
    EXPECT_EQ(bodyOf(rmc(sampleState())),
              "GPRMC,123519.00,A,4807.0380,N,01131.0000,E,22.4,84.4,230394,,");
}

TEST(SentenceBuilder, Req001_HdtLayout) {
    EXPECT_EQ(bodyOf(hdt(sampleState())), "HEHDT,84.4,T");
}

TEST(SentenceBuilder, Req001_DptLayout) {
    EXPECT_EQ(bodyOf(dpt(sampleState())), "SDDPT,12.3,0.0");
}

TEST(SentenceBuilder, Req001_SentencesStartWithDollarAndEndWithCrLf) {
    ShipState s = sampleState();
    for (const auto& str : {gga(s), rmc(s), hdt(s), dpt(s)}) {
        EXPECT_EQ(str.front(), '$');
        EXPECT_EQ(str.substr(str.size() - 2), "\r\n");
    }
}

// REQ-002: latitude ddmm.mmmm, longitude dddmm.mmmm, with N/S/E/W.
TEST(PositionFormat, Req002_LatitudeIsDdMmMmmm) {
    EXPECT_EQ(fmtLat(48.1173), "4807.0380");
    EXPECT_EQ(fmtLat(5.5), "0530.0000");       // leading zero on degrees
    EXPECT_EQ(fmtLat(0.0), "0000.0000");
    EXPECT_EQ(fmtLat(89.0), "8900.0000");
    EXPECT_EQ(fmtLat(48.1173).size(), 9u);
    EXPECT_EQ(fmtLat(48.1173)[4], '.');
}

TEST(PositionFormat, Req002_LongitudeIsDddMmMmmm) {
    EXPECT_EQ(fmtLon(11.516666667), "01131.0000");
    EXPECT_EQ(fmtLon(5.5), "00530.0000");
    EXPECT_EQ(fmtLon(0.0), "00000.0000");
    EXPECT_EQ(fmtLon(123.25), "12315.0000");
    EXPECT_EQ(fmtLon(179.9999), "17959.9940");
    EXPECT_EQ(fmtLon(11.5).size(), 10u);
    EXPECT_EQ(fmtLon(11.5)[5], '.');
}

TEST(PositionFormat, Req002_NegativeValuesFormatAsMagnitude) {
    EXPECT_EQ(fmtLat(-48.1173), fmtLat(48.1173));
    EXPECT_EQ(fmtLon(-11.5), fmtLon(11.5));
}

TEST(PositionFormat, Req002_MinutesNeverRenderAsSixty) {
    // 0.99999999 deg is 59.9999994 minutes, which would print as 60.0000.
    EXPECT_EQ(fmtLat(10.99999999), "1100.0000");
    EXPECT_EQ(fmtLon(10.99999999), "01100.0000");
    EXPECT_EQ(fmtLat(0.99999999), "0100.0000");
}

TEST(PositionFormat, Req002_HemisphereIndicators) {
    ShipState s = sampleState();

    s.lat = 48.0; s.lon = 11.0;
    EXPECT_NE(gga(s).find(",4800.0000,N,01100.0000,E,"), std::string::npos) << gga(s);

    s.lat = -48.0; s.lon = 11.0;
    EXPECT_NE(gga(s).find(",4800.0000,S,01100.0000,E,"), std::string::npos) << gga(s);

    s.lat = 48.0; s.lon = -11.0;
    EXPECT_NE(gga(s).find(",4800.0000,N,01100.0000,W,"), std::string::npos) << gga(s);

    s.lat = -48.0; s.lon = -11.0;
    EXPECT_NE(gga(s).find(",4800.0000,S,01100.0000,W,"), std::string::npos) << gga(s);
}

TEST(PositionFormat, Req002_HemisphereIndicatorsInRmc) {
    ShipState s = sampleState();
    s.lat = -33.8688; s.lon = -151.2093;
    EXPECT_NE(rmc(s).find(",A,3352.1280,S,15112.5580,W,"), std::string::npos) << rmc(s);
}
