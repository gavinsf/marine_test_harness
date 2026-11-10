#include <gtest/gtest.h>
#include "common/checksum.hpp"

TEST(Checksum, SameInputGivesSameOutput) {
    EXPECT_EQ(checksum("1"), checksum("1"));
}