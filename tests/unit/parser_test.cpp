#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "sim/builder.hpp"
#include "sim/parser.hpp"

namespace {

// Independent of checksum() so these tests do not depend on the code they exercise.
std::string withChecksum(const std::string& body, char start = '$', const char* tail = "\r\n") {
    unsigned cs = 0;
    for (char c : body) cs ^= static_cast<unsigned char>(c);
    char buf[8];
    std::snprintf(buf, sizeof buf, "*%02X", cs);
    return std::string(1, start) + body + buf + tail;
}

struct Result {
    std::optional<ParseError> error;
    Sentence sentence;
    std::string detail;
    bool ok() const { return !error.has_value(); }
};

Result run(const std::string& line) {
    NMEAParser p;
    Result r;
    r.error = p.parse(line, r.sentence);
    r.detail = p.last_detail();
    return r;
}

Result runBody(const std::string& body) { return run(withChecksum(body)); }

::testing::AssertionResult failsWith(const Result& r, ParseError code) {
    if (r.ok()) return ::testing::AssertionFailure() << "parsed OK, expected " << to_string(code);
    if (*r.error != code) {
        return ::testing::AssertionFailure() << "got " << to_string(*r.error) << " (" << r.detail
                                             << "), expected " << to_string(code);
    }
    return ::testing::AssertionSuccess();
}

const std::string kGga = "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,";
const std::string kRmc = "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W";

// Replace the n-th comma-separated field (0 = address) of a body.
std::string withField(std::string body, std::size_t n, const std::string& value) {
    std::size_t start = 0;
    for (std::size_t i = 0; i < n; ++i) start = body.find(',', start) + 1;
    std::size_t end = body.find(',', start);
    body.replace(start, end == std::string::npos ? std::string::npos : end - start, value);
    return body;
}

template <class T>
const T& as(const Result& r) { return std::get<T>(r.sentence); }

}  // namespace

// ================================================================ stage 1: frame check

TEST(ParserFrame, ValidFrameIsAccepted) {
    NMEAParser p;
    EXPECT_EQ(p.frame_check(withChecksum("HEHDT,274.07,T")), std::nullopt);
}

TEST(ParserFrame, EmptyInput) {
    NMEAParser p;
    EXPECT_EQ(p.frame_check(""), ParseError::Malformed);
    EXPECT_EQ(p.frame_check("\r\n"), ParseError::Malformed);
}

TEST(ParserFrame, MustStartWithDollarOrBang) {
    NMEAParser p;
    EXPECT_EQ(p.frame_check("GPHDT,1.0,T*00"), ParseError::Malformed);
    EXPECT_EQ(p.frame_check(" $GPHDT,1.0,T*00"), ParseError::Malformed);
    EXPECT_EQ(p.frame_check("#GPHDT,1.0,T*00"), ParseError::Malformed);
    EXPECT_EQ(p.frame_check(withChecksum("AIVDM,1,1,,A,13u,0", '!')), std::nullopt);
}

TEST(ParserFrame, BangSentencesGetAsFarAsDispatch) {
    // AIS-style: frame and checksum are fine, the type is just not one we decode.
    EXPECT_TRUE(failsWith(run(withChecksum("AIVDM,1,1,,A,13u,0", '!')), ParseError::UnsupportedSentence));
}

TEST(ParserFrame, ChecksumDelimiterAndDigits) {
    NMEAParser p;
    EXPECT_EQ(p.frame_check("$GPHDT,1.0,T"), ParseError::Malformed);        // no '*'
    EXPECT_EQ(p.frame_check("$"), ParseError::Malformed);
    EXPECT_EQ(p.frame_check("$GPHDT,1.0,T*"), ParseError::Malformed);       // no digits
    EXPECT_EQ(p.frame_check("$GPHDT,1.0,T*4"), ParseError::Malformed);      // one digit
    EXPECT_EQ(p.frame_check("$GPHDT,1.0,T*414"), ParseError::Malformed);    // three digits
    EXPECT_EQ(p.frame_check("$GPHDT,1.0,T*ZZ"), ParseError::Malformed);     // not hex
    EXPECT_EQ(p.frame_check("$GPHDT,1.0,T*4G"), ParseError::Malformed);
    EXPECT_EQ(p.frame_check("$GPHDT,1.0*T,T*41"), ParseError::Malformed);   // second '*'
}

TEST(ParserFrame, LengthLimitAllowsSlack) {
    auto padded = [](std::size_t total) {   // '$' + body + '*' + 2 hex digits == total
        std::string body = "GPXXX,";
        body.append(total - 1 - body.size() - 3, 'A');
        return withChecksum(body, '$', "");
    };
    ASSERT_EQ(padded(90).size(), kMaxAcceptedLength);
    NMEAParser p;
    EXPECT_EQ(p.frame_check(padded(82)), std::nullopt);
    EXPECT_EQ(p.frame_check(padded(90)), std::nullopt);       // 82 + slack
    EXPECT_EQ(p.frame_check(padded(91)), ParseError::TooLong);
    EXPECT_EQ(p.frame_check(padded(90) + "\r\n"), std::nullopt);   // CR/LF is not counted
    EXPECT_EQ(p.frame_check(std::string(500, 'x')), ParseError::Malformed);  // start char checked first
}

TEST(ParserFrame, TrailingLineEndingsAreStripped) {
    const std::string body = "HEHDT,274.07,T";
    EXPECT_TRUE(run(withChecksum(body, '$', "\r\n")).ok());
    EXPECT_TRUE(run(withChecksum(body, '$', "\n")).ok());
    EXPECT_TRUE(run(withChecksum(body, '$', "\r")).ok());
    EXPECT_TRUE(run(withChecksum(body, '$', "")).ok());
}

TEST(ParserFrame, ControlAndNonAsciiBytesAreRejected) {
    NMEAParser p;
    EXPECT_EQ(p.frame_check(withChecksum("HEHDT,274.\t07,T")), ParseError::Malformed);
    EXPECT_EQ(p.frame_check(withChecksum(std::string("HEHDT,274.07,T\x80"))), ParseError::Malformed);
    EXPECT_EQ(p.frame_check(withChecksum("HEHDT,274.07,T") + "junk"), ParseError::Malformed);
    EXPECT_EQ(p.frame_check("$HE$DT,274.07,T*00"), ParseError::Malformed);
}

// ================================================================ stage 2: checksum

TEST(ParserChecksum, KnownGoodSentencesFromTheSpecExamples) {
    NMEAParser p;
    EXPECT_EQ(p.checksum_check("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"), std::nullopt);
    EXPECT_EQ(p.checksum_check("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n"), std::nullopt);
}

TEST(ParserChecksum, UpperAndLowerCaseHexAreBothAccepted) {
    NMEAParser p;
    const std::string body = "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W";   // checksum 6A
    EXPECT_EQ(p.checksum_check("$" + body + "*6A"), std::nullopt);
    EXPECT_EQ(p.checksum_check("$" + body + "*6a"), std::nullopt);
    EXPECT_TRUE(run("$" + body + "*6a\r\n").ok());
    EXPECT_EQ(p.checksum_check("$" + body + "*6b"), ParseError::BadChecksum);
}

TEST(ParserChecksum, MismatchIsBadChecksum) {
    NMEAParser p;
    EXPECT_EQ(p.checksum_check("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48"), ParseError::BadChecksum);
    EXPECT_EQ(p.checksum_check("$HEHDT,274.07,T*00"), ParseError::BadChecksum);
}

TEST(ParserChecksum, EveryFlippedBodyByteIsDetected) {
    NMEAParser p;
    const std::string good = withChecksum("HEHDT,274.07,T");
    ASSERT_EQ(p.checksum_check(good), std::nullopt);
    for (std::size_t i = 1; i < good.find('*'); ++i) {
        std::string bad = good;
        bad[i] = static_cast<char>(bad[i] ^ 0x01);
        EXPECT_EQ(p.checksum_check(bad), ParseError::BadChecksum) << "byte " << i;
    }
}

TEST(ParserChecksum, UnlocatableChecksumIsMalformedNotMismatch) {
    NMEAParser p;
    EXPECT_EQ(p.checksum_check("$GPHDT,1.0,T"), ParseError::Malformed);
    EXPECT_EQ(p.checksum_check("$GPHDT,1.0,T*4"), ParseError::Malformed);
    EXPECT_EQ(p.checksum_check("$GPHDT,1.0,T*ZZ"), ParseError::Malformed);
    EXPECT_EQ(p.checksum_check(""), ParseError::Malformed);
}

TEST(ParserChecksum, IsRejectedBeforeFieldContentIsLookedAt) {
    // Each of these is also wrong in a later stage, but the checksum error must win.
    EXPECT_TRUE(failsWith(run("$GPGGA,zzzz,not,a,sentence*00"), ParseError::BadChecksum));
    EXPECT_TRUE(failsWith(run("$HEHDT,1.0*00"), ParseError::BadChecksum));          // wrong field count
    EXPECT_TRUE(failsWith(run("$GPXYZ,1,2,3*00"), ParseError::BadChecksum));        // unsupported type
    EXPECT_TRUE(failsWith(run("$HEHDT,999.0,T*00"), ParseError::BadChecksum));      // out-of-range value
}

// ================================================================ stage 3: split fields

TEST(ParserFields, SplitsOnCommasAndKeepsEmptyFields) {
    NMEAParser p;
    std::vector<std::string_view> f;
    const std::string line = withChecksum("GPGGA,,4807.038,,,,1,,");   // views point into this
    ASSERT_EQ(p.get_fields(line, f), std::nullopt);
    ASSERT_EQ(f.size(), 9u);
    EXPECT_EQ(f[0], "GPGGA");
    EXPECT_EQ(f[1], "");
    EXPECT_EQ(f[2], "4807.038");
    EXPECT_EQ(f[3], "");
    EXPECT_EQ(f[6], "1");
    EXPECT_EQ(f[8], "");          // trailing empty field is kept
}

TEST(ParserFields, AddressIsTheFirstField) {
    NMEAParser p;
    std::vector<std::string_view> f;
    const std::string line = withChecksum("HEHDT,274.07,T");
    ASSERT_EQ(p.get_fields(line, f), std::nullopt);
    EXPECT_EQ(f, (std::vector<std::string_view>{"HEHDT", "274.07", "T"}));
}

TEST(ParserFields, BodyWithNoCommaIsOneField) {
    NMEAParser p;
    std::vector<std::string_view> f;
    const std::string line = withChecksum("GPGGA");
    ASSERT_EQ(p.get_fields(line, f), std::nullopt);
    EXPECT_EQ(f.size(), 1u);
}

TEST(ParserFields, ClearsPreviousOutputAndRejectsUnframedInput) {
    NMEAParser p;
    std::vector<std::string_view> f = {"stale", "data"};
    EXPECT_EQ(p.get_fields("garbage", f), ParseError::Malformed);
    EXPECT_TRUE(f.empty());
}

// ================================================================ stage 4: dispatch and field count

TEST(ParserDispatch, FieldCountIsCheckedPerType) {
    NMEAParser p;
    auto fields = [](const std::string& addr, int n) {   // address plus n-1 empty fields
        std::vector<std::string> store;
        std::string line = "$" + addr;
        for (int i = 1; i < n; ++i) line += ",";
        return line;
    };
    struct Case { const char* addr; int lo, hi; };
    for (Case c : {Case{"GPGGA", 15, 15}, Case{"GPRMC", 12, 13}, Case{"HEHDT", 3, 3},
                   Case{"SDDPT", 3, 4}, Case{"WIMWV", 6, 6}}) {
        for (int n = c.lo - 2; n <= c.hi + 2; ++n) {
            if (n < 1) continue;
            std::string line = fields(c.addr, n) + "*00";
            std::vector<std::string_view> f;
            ASSERT_EQ(p.get_fields(line, f), std::nullopt);
            auto err = p.field_count_check(f);
            if (n >= c.lo && n <= c.hi) {
                EXPECT_EQ(err, std::nullopt) << c.addr << " with " << n << " fields";
            } else {
                EXPECT_EQ(err, ParseError::WrongFieldCount) << c.addr << " with " << n << " fields";
            }
        }
    }
}

TEST(ParserDispatch, UnknownTypesAreUnsupportedNotMalformed) {
    EXPECT_TRUE(failsWith(runBody("GPGSV,3,1,11,03,03,111,00"), ParseError::UnsupportedSentence));
    EXPECT_TRUE(failsWith(runBody("GPZDA,201530.00,04,07,2002,00,00"), ParseError::UnsupportedSentence));
    EXPECT_TRUE(failsWith(runBody("PGRMZ,93,f,3"), ParseError::UnsupportedSentence));   // proprietary
}

TEST(ParserDispatch, MalformedAddressFields) {
    EXPECT_TRUE(failsWith(runBody(""), ParseError::Malformed));
    EXPECT_TRUE(failsWith(runBody("GPGG,1"), ParseError::Malformed));
    EXPECT_TRUE(failsWith(runBody("GPGGAA,1"), ParseError::Malformed));
    EXPECT_TRUE(failsWith(runBody("gpgga,1"), ParseError::Malformed));
    EXPECT_TRUE(failsWith(runBody("GP-GA,1"), ParseError::Malformed));
}

TEST(ParserDispatch, CountErrorIsReportedBeforeFieldContentIsChecked) {
    // The one field present is also invalid, but the count is wrong, so that error wins.
    EXPECT_TRUE(failsWith(runBody("HEHDT,999.0"), ParseError::WrongFieldCount));
}

TEST(ParserDispatch, CountErrorNamesTheExpectation) {
    auto r = runBody("HEHDT,1.0");
    ASSERT_TRUE(failsWith(r, ParseError::WrongFieldCount));
    EXPECT_NE(r.detail.find("HDT"), std::string::npos);
    EXPECT_NE(r.detail.find("expected 3"), std::string::npos);
}

TEST(ParserDispatch, TalkerAndTypeAreSplit) {
    auto r = runBody("GNGGA,,,,,,,,,,,,,,");
    ASSERT_TRUE(r.ok()) << r.detail;
    EXPECT_EQ(typeOf(r.sentence), "GGA");
    EXPECT_EQ(talkerOf(r.sentence), "GN");
    EXPECT_TRUE(std::holds_alternative<Gga>(r.sentence));
}

TEST(ParserDispatch, EachSupportedTypeProducesItsStruct) {
    EXPECT_TRUE(std::holds_alternative<Gga>(runBody(kGga).sentence));
    EXPECT_TRUE(std::holds_alternative<Rmc>(runBody(kRmc).sentence));
    EXPECT_TRUE(std::holds_alternative<Hdt>(runBody("HEHDT,274.07,T").sentence));
    EXPECT_TRUE(std::holds_alternative<Dpt>(runBody("SDDPT,12.3,0.5").sentence));
    EXPECT_TRUE(std::holds_alternative<Mwv>(runBody("WIMWV,214.8,R,0.1,K,A").sentence));
}

// ================================================================ stage 5: GGA

TEST(ParserGga, DecodesAllFields) {
    auto r = runBody(kGga);
    ASSERT_TRUE(r.ok()) << r.detail;
    const auto& g = as<Gga>(r);
    EXPECT_EQ(g.talker, "GP");
    ASSERT_TRUE(g.time);
    EXPECT_EQ(g.time->hour, 12);
    EXPECT_EQ(g.time->minute, 35);
    EXPECT_DOUBLE_EQ(g.time->second, 19.0);
    EXPECT_NEAR(*g.lat, 48.1173, 1e-6);
    EXPECT_NEAR(*g.lon, 11.516666667, 1e-6);
    EXPECT_EQ(*g.fix_quality, 1);
    EXPECT_EQ(*g.satellites, 8);
    EXPECT_DOUBLE_EQ(*g.hdop, 0.9);
    EXPECT_DOUBLE_EQ(*g.altitude_m, 545.4);
    EXPECT_DOUBLE_EQ(*g.geoid_separation_m, 46.9);
    EXPECT_FALSE(g.dgps_age_s);
    EXPECT_FALSE(g.dgps_station);
}

TEST(ParserGga, EmptyFieldsBecomeNullopt) {
    auto r = runBody("GPGGA,,,,,,,,,,,,,,");
    ASSERT_TRUE(r.ok()) << r.detail;
    const auto& g = as<Gga>(r);
    EXPECT_FALSE(g.time);
    EXPECT_FALSE(g.lat);
    EXPECT_FALSE(g.lon);
    EXPECT_FALSE(g.fix_quality);
    EXPECT_FALSE(g.satellites);
    EXPECT_FALSE(g.hdop);
    EXPECT_FALSE(g.altitude_m);
    EXPECT_FALSE(g.geoid_separation_m);
}

TEST(ParserGga, SouthernAndWesternHemispheresAreNegative) {
    auto r = runBody(withField(withField(withField(withField(kGga, 3, "S"), 5, "W"), 2, "3351.000"), 4, "15112.000"));
    ASSERT_TRUE(r.ok()) << r.detail;
    EXPECT_NEAR(*as<Gga>(r).lat, -(33 + 51.0 / 60), 1e-9);
    EXPECT_NEAR(*as<Gga>(r).lon, -(151 + 12.0 / 60), 1e-9);
}

TEST(ParserGga, TimeFormat) {
    EXPECT_DOUBLE_EQ(as<Gga>(runBody(withField(kGga, 1, "123519.25"))).time->second, 19.25);
    for (const char* bad : {"246100", "126100", "123560", "1235", "12a519", "123519.", "123519.x", "1235190", "-12351"}) {
        EXPECT_TRUE(failsWith(runBody(withField(kGga, 1, bad)), ParseError::InvalidField)) << bad;
    }
    for (const char* ok : {"000000", "235959", "235959.99", "000000.0"}) {
        EXPECT_TRUE(runBody(withField(kGga, 1, ok)).ok()) << ok;
    }
}

TEST(ParserGga, LatitudeFormatAndRange) {
    for (const char* bad : {"480.038", "48070.38", "4860.000", "9001.000", "9100.000", "48.07038", "48a7.038",
                            "4807.", "-4807.038", "4807.0x8"}) {
        EXPECT_TRUE(failsWith(runBody(withField(kGga, 2, bad)), ParseError::InvalidField)) << bad;
    }
    for (const char* ok : {"9000.000", "0000.000", "4807", "4807.0380", "4859.9999"}) {
        EXPECT_TRUE(runBody(withField(kGga, 2, ok)).ok()) << ok;
    }
}

TEST(ParserGga, LongitudeFormatAndRange) {
    for (const char* bad : {"1131.000", "011310.00", "01160.000", "18001.000", "19000.000", "0113a.000"}) {
        EXPECT_TRUE(failsWith(runBody(withField(kGga, 4, bad)), ParseError::InvalidField)) << bad;
    }
    EXPECT_TRUE(runBody(withField(kGga, 4, "18000.000")).ok());
    EXPECT_TRUE(runBody(withField(kGga, 4, "00000.000")).ok());
}

TEST(ParserGga, HemisphereMustMatchAndAccompanyItsValue) {
    EXPECT_TRUE(failsWith(runBody(withField(kGga, 3, "E")), ParseError::InvalidField));    // E on a latitude
    EXPECT_TRUE(failsWith(runBody(withField(kGga, 3, "n")), ParseError::InvalidField));    // lowercase
    EXPECT_TRUE(failsWith(runBody(withField(kGga, 3, "")), ParseError::InvalidField));     // value, no hemisphere
    EXPECT_TRUE(failsWith(runBody(withField(kGga, 5, "N")), ParseError::InvalidField));
    EXPECT_TRUE(failsWith(runBody(withField(kGga, 2, "")), ParseError::InvalidField));     // hemisphere, no value
}

TEST(ParserGga, NumericFieldsMustBePlainNumbersInRange) {
    struct Case { std::size_t field; const char* value; bool ok; };
    for (Case c : {Case{6, "0", true}, Case{6, "8", true}, Case{6, "9", false}, Case{6, "x", false}, Case{6, "-1", false},
                   Case{7, "99", true}, Case{7, "100", false},
                   Case{8, "0.0", true}, Case{8, "abc", false}, Case{8, "-0.5", false},
                   Case{9, "-12.5", true}, Case{9, "1e3", false}, Case{9, "inf", false}, Case{9, "nan", false},
                   Case{9, "1.2.3", false}, Case{9, "+5", false}, Case{9, "99999", false}, Case{9, "-", false}, Case{9, ".", false}}) {
        EXPECT_EQ(runBody(withField(kGga, c.field, c.value)).ok(), c.ok) << "field " << c.field << " = " << c.value;
    }
}

TEST(ParserGga, UnitsMustBeMetresAndAccompanyValues) {
    EXPECT_TRUE(failsWith(runBody(withField(kGga, 10, "F")), ParseError::InvalidField));
    EXPECT_TRUE(failsWith(runBody(withField(kGga, 10, "")), ParseError::InvalidField));   // altitude without unit
    EXPECT_TRUE(failsWith(runBody(withField(kGga, 12, "")), ParseError::InvalidField));   // geoid without unit
    // A unit with no value is what the builder emits for geoid separation.
    EXPECT_TRUE(runBody(withField(kGga, 11, "")).ok());
}

TEST(ParserGga, DgpsFields) {
    auto r = runBody(withField(withField(kGga, 13, "3.5"), 14, "0123"));
    ASSERT_TRUE(r.ok()) << r.detail;
    EXPECT_DOUBLE_EQ(*as<Gga>(r).dgps_age_s, 3.5);
    EXPECT_EQ(*as<Gga>(r).dgps_station, 123);
    EXPECT_TRUE(failsWith(runBody(withField(kGga, 14, "1024")), ParseError::InvalidField));
}

TEST(ParserGga, ErrorDetailNamesTheOffendingField) {
    auto r = runBody(withField(kGga, 2, "9100.000"));
    ASSERT_TRUE(failsWith(r, ParseError::InvalidField));
    EXPECT_NE(r.detail.find("latitude"), std::string::npos) << r.detail;
    EXPECT_NE(r.detail.find("9100.000"), std::string::npos) << r.detail;
}

// ================================================================ stage 5: RMC

TEST(ParserRmc, DecodesAllFields) {
    auto r = runBody(kRmc);
    ASSERT_TRUE(r.ok()) << r.detail;
    const auto& m = as<Rmc>(r);
    EXPECT_EQ(*m.status, 'A');
    EXPECT_NEAR(*m.lat, 48.1173, 1e-6);
    EXPECT_NEAR(*m.lon, 11.516666667, 1e-6);
    EXPECT_DOUBLE_EQ(*m.sog_knots, 22.4);
    EXPECT_DOUBLE_EQ(*m.cog_deg, 84.4);
    EXPECT_EQ(m.date->day, 23);
    EXPECT_EQ(m.date->month, 3);
    EXPECT_EQ(m.date->year, 94);
    EXPECT_DOUBLE_EQ(*m.magnetic_variation_deg, -3.1);   // W is negative
    EXPECT_FALSE(m.mode);                                 // 12-field (pre-2.3) form
}

TEST(ParserRmc, ModeIndicatorIsOptionalAndValidated) {
    auto r = runBody(kRmc + ",A");
    ASSERT_TRUE(r.ok()) << r.detail;
    EXPECT_EQ(*as<Rmc>(r).mode, 'A');
    EXPECT_TRUE(failsWith(runBody(kRmc + ",Z"), ParseError::InvalidField));
    EXPECT_TRUE(runBody(kRmc + ",").ok());
}

TEST(ParserRmc, MagneticVariation) {
    EXPECT_DOUBLE_EQ(*as<Rmc>(runBody(withField(kRmc, 11, "E"))).magnetic_variation_deg, 3.1);
    EXPECT_TRUE(failsWith(runBody(withField(kRmc, 11, "")), ParseError::InvalidField));     // value, no direction
    EXPECT_TRUE(failsWith(runBody(withField(kRmc, 10, "181")), ParseError::InvalidField));
    auto none = runBody(withField(withField(kRmc, 10, ""), 11, ""));
    ASSERT_TRUE(none.ok());
    EXPECT_FALSE(as<Rmc>(none).magnetic_variation_deg);
}

TEST(ParserRmc, StatusMustBeAOrV) {
    EXPECT_TRUE(runBody(withField(kRmc, 2, "V")).ok());
    for (const char* bad : {"X", "a", "AV", "1"}) {
        EXPECT_TRUE(failsWith(runBody(withField(kRmc, 2, bad)), ParseError::InvalidField)) << bad;
    }
}

TEST(ParserRmc, SpeedAndCourseRanges) {
    EXPECT_TRUE(failsWith(runBody(withField(kRmc, 7, "-1.0")), ParseError::InvalidField));
    EXPECT_TRUE(failsWith(runBody(withField(kRmc, 7, "1000")), ParseError::InvalidField));
    EXPECT_TRUE(failsWith(runBody(withField(kRmc, 8, "360.1")), ParseError::InvalidField));
    EXPECT_TRUE(runBody(withField(kRmc, 8, "360.0")).ok());
    EXPECT_TRUE(runBody(withField(kRmc, 8, "0")).ok());
}

TEST(ParserRmc, DateFormatAndCalendar) {
    for (const char* bad : {"320394", "000394", "231394", "230094", "300294", "290294", "23039", "2303944", "23o394", "310494"}) {
        EXPECT_TRUE(failsWith(runBody(withField(kRmc, 9, bad)), ParseError::InvalidField)) << bad;
    }
    for (const char* ok : {"230394", "290296", "310195", "300496", "010100"}) {
        EXPECT_TRUE(runBody(withField(kRmc, 9, ok)).ok()) << ok;
    }
}

TEST(ParserRmc, VoidSentenceWithEmptyPositionIsValid) {
    auto r = runBody("GPRMC,123519,V,,,,,,,230394,,");
    ASSERT_TRUE(r.ok()) << r.detail;
    EXPECT_EQ(*as<Rmc>(r).status, 'V');
    EXPECT_FALSE(as<Rmc>(r).lat);
    EXPECT_FALSE(as<Rmc>(r).sog_knots);
    EXPECT_TRUE(as<Rmc>(r).date);
}

// ================================================================ stage 5: HDT, DPT, MWV

TEST(ParserHdt, DecodesAndValidates) {
    EXPECT_DOUBLE_EQ(*as<Hdt>(runBody("HEHDT,274.07,T")).heading_true_deg, 274.07);
    EXPECT_TRUE(runBody("HEHDT,360.0,T").ok());
    EXPECT_TRUE(runBody("HEHDT,0.0,T").ok());
    EXPECT_TRUE(runBody("HEHDT,,T").ok());
    auto empty = runBody("HEHDT,,");
    ASSERT_TRUE(empty.ok());
    EXPECT_FALSE(as<Hdt>(empty).heading_true_deg);
    for (const char* bad : {"HEHDT,360.1,T", "HEHDT,-1,T", "HEHDT,274.07,M", "HEHDT,274.07,", "HEHDT,abc,T"}) {
        EXPECT_TRUE(failsWith(runBody(bad), ParseError::InvalidField)) << bad;
    }
}

TEST(ParserDpt, DecodesAndValidates) {
    auto r = runBody("SDDPT,12.3,-0.5,200");
    ASSERT_TRUE(r.ok()) << r.detail;
    EXPECT_DOUBLE_EQ(*as<Dpt>(r).depth_m, 12.3);
    EXPECT_DOUBLE_EQ(*as<Dpt>(r).offset_m, -0.5);
    EXPECT_DOUBLE_EQ(*as<Dpt>(r).max_range_m, 200.0);
    EXPECT_TRUE(runBody("SDDPT,12.3,0.0").ok());
    EXPECT_TRUE(runBody("SDDPT,,").ok());
    for (const char* bad : {"SDDPT,-1.0,0.0", "SDDPT,12000,0.0", "SDDPT,12.3,101", "SDDPT,12.3,0.0,-5", "SDDPT,deep,0.0"}) {
        EXPECT_TRUE(failsWith(runBody(bad), ParseError::InvalidField)) << bad;
    }
}

TEST(ParserMwv, DecodesAndValidates) {
    auto r = runBody("WIMWV,214.8,R,0.1,K,A");
    ASSERT_TRUE(r.ok()) << r.detail;
    const auto& m = as<Mwv>(r);
    EXPECT_DOUBLE_EQ(*m.angle_deg, 214.8);
    EXPECT_EQ(*m.reference, 'R');
    EXPECT_DOUBLE_EQ(*m.speed, 0.1);
    EXPECT_EQ(*m.speed_units, 'K');
    EXPECT_EQ(*m.status, 'A');

    EXPECT_TRUE(runBody("WIMWV,360.0,T,12.0,N,V").ok());
    EXPECT_TRUE(runBody("WIMWV,,,,,").ok());
    for (const char* u : {"K", "M", "N", "S"}) EXPECT_TRUE(runBody(std::string("WIMWV,10,R,5,") + u + ",A").ok()) << u;
    for (const char* bad : {"WIMWV,361,R,5,N,A", "WIMWV,10,X,5,N,A", "WIMWV,10,,5,N,A", "WIMWV,10,R,5,X,A",
                            "WIMWV,10,R,5,,A", "WIMWV,10,R,-5,N,A", "WIMWV,10,R,5,N,X"}) {
        EXPECT_TRUE(failsWith(runBody(bad), ParseError::InvalidField)) << bad;
    }
}

// ================================================================ whole pipeline

TEST(ParserPipeline, ParseWithoutOutputGivesTheSameVerdict) {
    NMEAParser p;
    EXPECT_EQ(p.parse(withChecksum(kGga)), std::nullopt);
    EXPECT_EQ(p.parse(withChecksum(withField(kGga, 2, "9100.000"))), ParseError::InvalidField);
    EXPECT_EQ(p.parse("junk"), ParseError::Malformed);
}

TEST(ParserPipeline, OutputIsLeftUntouchedOnFailure) {
    NMEAParser p;
    Sentence s = Hdt{"HE", 90.0};
    EXPECT_EQ(p.parse(withChecksum("HEHDT,999.0,T"), s), ParseError::InvalidField);
    ASSERT_TRUE(std::holds_alternative<Hdt>(s));
    EXPECT_DOUBLE_EQ(*std::get<Hdt>(s).heading_true_deg, 90.0);
}

TEST(ParserPipeline, ParserIsReusableAndDetailIsClearedOnSuccess) {
    NMEAParser p;
    Sentence s;
    EXPECT_EQ(p.parse(withChecksum("HEHDT,999.0,T"), s), ParseError::InvalidField);
    EXPECT_FALSE(p.last_detail().empty());
    EXPECT_EQ(p.parse(withChecksum("HEHDT,10.0,T"), s), std::nullopt);
    EXPECT_TRUE(p.last_detail().empty());
}

TEST(ParserPipeline, EveryPrefixOfASentenceIsRejected) {
    NMEAParser p;
    for (const std::string& full : {withChecksum(kGga), withChecksum(kRmc), withChecksum("HEHDT,274.07,T")}) {
        const std::size_t star = full.find('*');
        for (std::size_t len = 0; len < star + 3; ++len) {
            EXPECT_TRUE(p.parse(full.substr(0, len)).has_value()) << "prefix " << len << " of " << full;
        }
        EXPECT_EQ(p.parse(full.substr(0, star + 3)), std::nullopt);   // complete, CR/LF cut off
    }
}

TEST(ParserPipeline, RandomBytesNeverCrash) {
    NMEAParser p;
    unsigned seed = 12345;
    auto next = [&] { return seed = seed * 1103515245u + 12345u; };
    for (int i = 0; i < 3000; ++i) {
        std::string junk;
        const int len = static_cast<int>(next() >> 16) % 100;
        for (int j = 0; j < len; ++j) junk.push_back(static_cast<char>(next() >> 16));
        (void)p.parse(junk);
        (void)p.parse("$" + junk);
        (void)p.parse("$" + junk + "*4A");
        (void)p.parse(withChecksum("GPGGA," + junk));      // valid frame and checksum, garbage fields
        (void)p.parse(withChecksum("GPRMC," + junk));
    }
    SUCCEED();
}

TEST(ParserPipeline, ErrorNamesAreDistinct) {
    std::set<std::string> names;
    for (ParseError e : {ParseError::Malformed, ParseError::BadChecksum, ParseError::WrongFieldCount,
                         ParseError::InvalidField, ParseError::UnsupportedSentence, ParseError::TooLong}) {
        names.insert(to_string(e));
    }
    EXPECT_EQ(names.size(), 6u);
}

// ================================================================ builder round trip

namespace {
ShipState state(double lat, double lon) {
    ShipState s{};
    s.lat = lat; s.lon = lon; s.sog = 12.3; s.cog = 271.5; s.heading = 270.0; s.depth = 18.7;
    s.wind_angle = 123.4; s.wind_speed = 17.2;
    s.utc.tm_hour = 7; s.utc.tm_min = 5; s.utc.tm_sec = 9;
    s.utc.tm_mday = 31; s.utc.tm_mon = 11; s.utc.tm_year = 125;
    s.centis = 25;
    return s;
}
}  // namespace

TEST(ParserRoundTrip, BuilderSentencesParseBackToTheSameValues) {
    for (auto [lat, lon] : {std::pair{47.6062, -122.3321}, std::pair{-33.8688, 151.2093}, std::pair{0.0, 0.0},
                            std::pair{89.99, 179.99}, std::pair{-0.5, -0.5}}) {
        ShipState s = state(lat, lon);

        auto g = run(gga(s));
        ASSERT_TRUE(g.ok()) << gga(s) << g.detail;
        EXPECT_EQ(as<Gga>(g).talker, "GP");
        EXPECT_NEAR(*as<Gga>(g).lat, lat, 1e-4);
        EXPECT_NEAR(*as<Gga>(g).lon, lon, 1e-4);
        EXPECT_EQ(as<Gga>(g).time->hour, 7);
        EXPECT_EQ(as<Gga>(g).time->minute, 5);
        EXPECT_DOUBLE_EQ(as<Gga>(g).time->second, 9.25);

        auto r = run(rmc(s));
        ASSERT_TRUE(r.ok()) << rmc(s) << r.detail;
        EXPECT_EQ(as<Rmc>(r).talker, "GP");
        EXPECT_NEAR(*as<Rmc>(r).lat, lat, 1e-4);
        EXPECT_NEAR(*as<Rmc>(r).lon, lon, 1e-4);
        EXPECT_DOUBLE_EQ(*as<Rmc>(r).sog_knots, 12.3);
        EXPECT_DOUBLE_EQ(*as<Rmc>(r).cog_deg, 271.5);
        EXPECT_EQ(as<Rmc>(r).date->day, 31);
        EXPECT_EQ(as<Rmc>(r).date->month, 12);
        EXPECT_EQ(as<Rmc>(r).date->year, 25);

        auto h = run(hdt(s));
        ASSERT_TRUE(h.ok()) << hdt(s) << h.detail;
        EXPECT_EQ(talkerOf(h.sentence), "HE");
        EXPECT_DOUBLE_EQ(*as<Hdt>(h).heading_true_deg, 270.0);

        auto d = run(dpt(s));
        ASSERT_TRUE(d.ok()) << dpt(s) << d.detail;
        EXPECT_EQ(talkerOf(d.sentence), "SD");
        EXPECT_DOUBLE_EQ(*as<Dpt>(d).depth_m, 18.7);

        auto m = run(mwv(s));
        ASSERT_TRUE(m.ok()) << mwv(s) << m.detail;
        EXPECT_EQ(talkerOf(m.sentence), "WI");
        EXPECT_DOUBLE_EQ(*as<Mwv>(m).angle_deg, 123.4);
        EXPECT_DOUBLE_EQ(*as<Mwv>(m).speed, 17.2);
    }
}

TEST(ParserRoundTrip, ValuesThatRoundUpToTheLimitStayValid) {
    ShipState s = state(47.0, -122.0);
    s.cog = 359.96;            // formats as "360.0"
    s.heading = 359.96;
    s.wind_angle = 359.99;
    EXPECT_TRUE(run(rmc(s)).ok()) << rmc(s);
    EXPECT_TRUE(run(hdt(s)).ok()) << hdt(s);
    EXPECT_TRUE(run(mwv(s)).ok()) << mwv(s);
}

TEST(ParserRoundTrip, CorruptedBuilderOutputIsCaughtAtTheRightStage) {
    ShipState s = state(47.6, -122.3);
    std::string good = gga(s);
    std::string badSum = good;
    badSum[badSum.find('*') + 1] = badSum[badSum.find('*') + 1] == '0' ? '1' : '0';
    EXPECT_TRUE(failsWith(run(badSum), ParseError::BadChecksum));
    EXPECT_TRUE(failsWith(run(good.substr(0, good.find('*'))), ParseError::Malformed));     // truncated before '*'
    EXPECT_TRUE(failsWith(run(good.substr(0, good.find('*') + 2)), ParseError::Malformed)); // one checksum digit
}
