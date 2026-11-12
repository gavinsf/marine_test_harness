#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "sim/ground_truth.hpp"
#include "sim/injector.hpp"

namespace {

Outgoing clean() {
    Outgoing o;
    o.seq = 12;
    o.fault = Fault::None;
    o.original = o.text = "$HEHDT,90.0,T*2B\r\n";
    o.emitIndex = 9;
    return o;
}

std::vector<std::string> lines(const std::string& path) {
    std::ifstream in(path);
    std::vector<std::string> v;
    for (std::string l; std::getline(in, l);) v.push_back(l);
    return v;
}

std::string tempPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

TEST(GroundTruth, JsonLineHasEveryFieldForASentMessage) {
    EXPECT_EQ(toJsonLine(clean(), 1.2),
              "{\"seq\":12,\"copy\":0,\"emit\":9,\"t\":1.200,\"fault\":\"none\",\"dropped\":false,"
              "\"original\":\"$HEHDT,90.0,T*2B\\r\\n\",\"text\":\"$HEHDT,90.0,T*2B\\r\\n\"}");
}

TEST(GroundTruth, DroppedMessageHasNullEmitAndEmptyText) {
    Outgoing o = clean();
    o.fault = Fault::Drop;
    o.dropped = true;
    o.text.clear();
    const std::string line = toJsonLine(o, 0.0);
    EXPECT_NE(line.find("\"emit\":null"), std::string::npos);
    EXPECT_NE(line.find("\"dropped\":true"), std::string::npos);
    EXPECT_NE(line.find("\"fault\":\"drop\""), std::string::npos);
    EXPECT_NE(line.find("\"text\":\"\""), std::string::npos);
    EXPECT_NE(line.find("\"original\":\"$HEHDT"), std::string::npos);
}

TEST(GroundTruth, DuplicateCopyAndFaultNameAreRecorded) {
    Outgoing o = clean();
    o.fault = Fault::Duplicate;
    o.copy = 1;
    const std::string line = toJsonLine(o, 0.5);
    EXPECT_NE(line.find("\"copy\":1"), std::string::npos);
    EXPECT_NE(line.find("\"fault\":\"duplicate\""), std::string::npos);
}

TEST(GroundTruth, QuotesBackslashesAndControlOrHighBytesAreEscaped) {
    Outgoing o = clean();
    o.text = std::string("a\"b\\c\td\x01") + "\xE9" + "\x7F";
    const std::string line = toJsonLine(o, 0.0);
    EXPECT_NE(line.find("\"text\":\"a\\\"b\\\\c\\u0009d\\u0001\\u00E9\\u007F\""), std::string::npos) << line;
    EXPECT_EQ(line.find('\n'), std::string::npos);
    EXPECT_EQ(line.find('\t'), std::string::npos);
}

TEST(GroundTruth, TimeIsWrittenWithMillisecondPrecision) {
    EXPECT_NE(toJsonLine(clean(), 3.14159).find("\"t\":3.142"), std::string::npos);
}

TEST(GroundTruth, LogWritesOneLinePerMessageAndIsReadableImmediately) {
    const std::string path = tempPath("nmea_gt_test_lines.jsonl");
    {
        GroundTruthLog log(path);
        EXPECT_TRUE(log.enabled());
        log.write(clean(), 0.0);
        Outgoing second = clean();
        second.seq = 13;
        log.write(second, 0.1);
        const auto v = lines(path);          // flushed per write, log still open
        ASSERT_EQ(v.size(), 2u);
        EXPECT_EQ(v[0], toJsonLine(clean(), 0.0));
        EXPECT_EQ(v[1], toJsonLine(second, 0.1));
    }
    std::remove(path.c_str());
}

TEST(GroundTruth, OpeningAnExistingLogTruncatesIt) {
    const std::string path = tempPath("nmea_gt_test_trunc.jsonl");
    {
        GroundTruthLog a(path);
        a.write(clean(), 0.0);
        a.write(clean(), 0.1);
    }
    {
        GroundTruthLog b(path);
        b.write(clean(), 0.0);
    }
    EXPECT_EQ(lines(path).size(), 1u);
    std::remove(path.c_str());
}

TEST(GroundTruth, EmptyPathDisablesLoggingWithoutError) {
    GroundTruthLog log("");
    EXPECT_FALSE(log.enabled());
    EXPECT_NO_THROW(log.write(clean(), 0.0));
}

TEST(GroundTruth, UnwritablePathThrows) {
    EXPECT_THROW(GroundTruthLog("/nonexistent_dir_for_nmea_test/log.jsonl"), std::runtime_error);
}
