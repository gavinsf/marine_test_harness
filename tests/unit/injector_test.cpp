#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/checksum.hpp"
#include "common/ship_state.hpp"
#include "sim/builder.hpp"
#include "sim/injector.hpp"
#include "sim/parser.hpp"

namespace {

// All faults off; a test switches on the one it is about.
Config none() {
    Config c;
    c.pBadChecksum = c.pTruncate = c.pDuplicate = c.pReorder = c.pDrop = c.pDelay = c.pBurst = 0.0;
    return c;
}

// A valid sentence that differs per index (GGA / HDT alternate, time advances).
std::string sentence(int i) {
    ShipState s{};
    s.lat = 47.6 + i * 1e-4;
    s.lon = -122.4;
    s.sog = 10.0;
    s.cog = s.heading = 90.0;
    s.utc.tm_year = 125;
    s.utc.tm_mday = 1;
    s.utc.tm_hour = 12;
    s.utc.tm_min = i / 60 % 60;
    s.utc.tm_sec = i % 60;
    return (i % 2) ? hdt(s) : gga(s);
}

// Feed `n` sentences at 10 per second, collect what each release() returns, then drain().
std::vector<Outgoing> run(Injector& inj, int n) {
    std::vector<Outgoing> all;
    for (int i = 0; i < n; ++i) {
        const double t = i * 0.1;
        inj.submit(static_cast<std::uint64_t>(i), t, sentence(i));
        for (Outgoing& o : inj.release(t)) all.push_back(std::move(o));
    }
    for (Outgoing& o : inj.drain()) all.push_back(std::move(o));
    return all;
}

std::vector<Outgoing> run(std::uint64_t seed, const Config& c, int n) {
    Injector inj(seed, c);
    return run(inj, n);
}

bool throwsInvalid(const Config& c) {
    try {
        Injector inj(1, c);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

}  // namespace

// ------------------------------------------------------------------ no faults

TEST(Injector, WithNoFaultsEverySentencePassesThroughUnchangedAndInOrder) {
    const auto v = run(1, none(), 100);
    ASSERT_EQ(v.size(), 100u);
    for (std::size_t i = 0; i < v.size(); ++i) {
        EXPECT_EQ(v[i].seq, i);
        EXPECT_EQ(v[i].copy, 0);
        EXPECT_EQ(v[i].fault, Fault::None);
        EXPECT_FALSE(v[i].dropped);
        EXPECT_EQ(v[i].text, sentence(static_cast<int>(i)));
        EXPECT_EQ(v[i].original, v[i].text);
        EXPECT_EQ(v[i].emitIndex, i);
    }
}

TEST(Injector, UnfaultedSentencesStillParse) {
    NMEAParser parser;
    for (const Outgoing& o : run(1, none(), 50)) {
        Sentence out;
        EXPECT_FALSE(parser.parse(o.text, out).has_value()) << o.text;
    }
}

TEST(Injector, NothingIsReleasedUntilSubmittedAndNothingTwice) {
    Injector inj(1, none());
    EXPECT_TRUE(inj.release(0.0).empty());
    inj.submit(0, 0.0, sentence(0));
    EXPECT_EQ(inj.release(0.0).size(), 1u);
    EXPECT_TRUE(inj.release(0.0).empty());
    EXPECT_TRUE(inj.drain().empty());
}

// ------------------------------------------------------------------ bad checksum

TEST(Injector, BadChecksumChangesOnlyTheChecksumDigits) {
    Config c = none();
    c.pBadChecksum = 1.0;
    NMEAParser parser;
    const auto v = run(2, c, 500);
    ASSERT_EQ(v.size(), 500u);
    for (const Outgoing& o : v) {
        EXPECT_EQ(o.fault, Fault::BadChecksum);
        EXPECT_NE(o.text, o.original);
        EXPECT_EQ(o.text.size(), o.original.size());
        EXPECT_EQ(o.text.substr(0, o.text.rfind('*')), o.original.substr(0, o.original.rfind('*')));
        EXPECT_EQ(o.text.substr(o.text.size() - 2), "\r\n");
        Sentence out;
        const auto err = parser.parse(o.text, out);
        ASSERT_TRUE(err.has_value()) << o.text;
        EXPECT_EQ(*err, ParseError::BadChecksum) << o.text;
    }
}

TEST(Injector, BadChecksumToleratesInputWithoutAChecksum) {
    Config c = none();
    c.pBadChecksum = 1.0;
    Injector inj(1, c);
    inj.submit(0, 0, "no star");
    inj.submit(1, 0, "");
    inj.submit(2, 0, "$X*");
    EXPECT_EQ(inj.drain().size(), 3u);
}

// ------------------------------------------------------------------ truncate

TEST(Injector, TruncateKeepsAProperPrefixAndTheLineEnding) {
    Config c = none();
    c.pTruncate = 1.0;
    NMEAParser parser;
    for (const Outgoing& o : run(3, c, 500)) {
        EXPECT_EQ(o.fault, Fault::Truncate);
        ASSERT_LT(o.text.size(), o.original.size());
        ASSERT_GE(o.text.size(), 3u);
        EXPECT_EQ(o.text.substr(o.text.size() - 2), "\r\n");
        const std::string body = o.text.substr(0, o.text.size() - 2);
        EXPECT_EQ(o.original.compare(0, body.size(), body), 0);
        Sentence out;
        EXPECT_TRUE(parser.parse(o.text, out).has_value()) << o.text;   // never accidentally valid
    }
}

TEST(Injector, TruncateToleratesTinyInputs) {
    Config c = none();
    c.pTruncate = 1.0;
    Injector inj(1, c);
    inj.submit(0, 0, "$A*00");
    inj.submit(1, 0, "");
    inj.submit(2, 0, "x");
    inj.submit(3, 0, "$GP\r\n");
    EXPECT_EQ(inj.drain().size(), 4u);
}

// ------------------------------------------------------------------ duplicate

TEST(Injector, DuplicateSendsEverySentenceTwiceAsCopyZeroAndOne) {
    Config c = none();
    c.pDuplicate = 1.0;
    const auto v = run(4, c, 200);
    ASSERT_EQ(v.size(), 400u);
    std::map<std::uint64_t, int> count, copySum;
    for (const Outgoing& o : v) {
        ++count[o.seq];
        copySum[o.seq] += o.copy;
        EXPECT_EQ(o.text, o.original);
        EXPECT_EQ(o.fault, Fault::Duplicate);
    }
    for (const auto& [seq, n] : count) {
        EXPECT_EQ(n, 2) << seq;
        EXPECT_EQ(copySum[seq], 1) << seq;
    }
}

// ------------------------------------------------------------------ drop

TEST(Injector, DroppedMessagesAreReturnedForLoggingButMarkedNotToSend) {
    Config c = none();
    c.pDrop = 1.0;
    const auto v = run(5, c, 100);
    ASSERT_EQ(v.size(), 100u);
    for (const Outgoing& o : v) {
        EXPECT_TRUE(o.dropped);
        EXPECT_EQ(o.fault, Fault::Drop);
        EXPECT_TRUE(o.text.empty());
        EXPECT_FALSE(o.original.empty());
    }
}

TEST(Injector, EmitIndexCountsOnlyMessagesThatAreActuallySent) {
    Config c = none();
    c.pDrop = 0.3;
    const auto v = run(6, c, 500);
    std::uint64_t expected = 0;
    for (const Outgoing& o : v)
        if (!o.dropped) { EXPECT_EQ(o.emitIndex, expected++); }
    EXPECT_GT(expected, 0u);
    EXPECT_LT(expected, 500u);
}

// ------------------------------------------------------------------ delay

TEST(Injector, DelayedMessageIsHeldUntilItsDueTime) {
    Config c = none();
    c.pDelay = 1.0;
    c.delayMsMin = 200;
    c.delayMsMax = 3000;
    Injector inj(6, c);
    inj.submit(0, 0.0, sentence(0));
    EXPECT_TRUE(inj.release(0.0).empty());
    EXPECT_TRUE(inj.release(0.19).empty());     // shorter than the minimum delay
    EXPECT_EQ(inj.release(3.0).size(), 1u);     // longer than the maximum delay
    EXPECT_TRUE(inj.release(100.0).empty());
}

TEST(Injector, DelaysOfDifferentLengthsDeliverOutOfOrder) {
    Config c = none();
    c.pDelay = 1.0;
    const auto v = run(7, c, 200);
    ASSERT_EQ(v.size(), 200u);
    bool outOfOrder = false;
    for (std::size_t i = 1; i < v.size(); ++i)
        if (v[i].seq < v[i - 1].seq) outOfOrder = true;
    EXPECT_TRUE(outOfOrder);
}

// ------------------------------------------------------------------ reorder

TEST(Injector, ReorderedMessageIsOvertakenByAtMostMaxReorderLaterOnes) {
    Config c = none();
    c.pReorder = 0.3;
    c.maxReorder = 3;
    const auto v = run(8, c, 1000);
    ASSERT_EQ(v.size(), 1000u);

    std::map<std::uint64_t, std::size_t> pos;
    for (std::size_t i = 0; i < v.size(); ++i) pos[v[i].seq] = i;
    ASSERT_EQ(pos.size(), 1000u);   // every sentence exactly once

    int overtaken = 0, maxOvertaken = 0;
    for (const Outgoing& o : v) {
        if (o.fault != Fault::Reorder) continue;
        int over = 0;
        for (const Outgoing& q : v)
            if (q.seq > o.seq && pos[q.seq] < pos[o.seq]) ++over;
        if (over > 0) ++overtaken;
        maxOvertaken = std::max(maxOvertaken, over);
    }
    EXPECT_LE(maxOvertaken, 3);
    EXPECT_GT(overtaken, 100);
}

TEST(Injector, ReorderLeavesEverythingElseInOrder) {
    Config c = none();
    c.pReorder = 0.3;
    const auto v = run(9, c, 500);
    std::uint64_t last = 0;
    bool first = true;
    for (const Outgoing& o : v) {
        if (o.fault == Fault::Reorder) continue;
        if (!first) { EXPECT_GT(o.seq, last); }
        last = o.seq;
        first = false;
    }
}

// ------------------------------------------------------------------ burst

TEST(Injector, BurstHoldsBetweenMinAndMaxSentencesAndReleasesThemTogether) {
    Config c = none();
    c.pBurst = 1.0;
    c.burstMin = 3;
    c.burstMax = 8;
    Injector inj(10, c);

    std::vector<std::size_t> groupSizes;
    for (int i = 0; i < 300; ++i) {
        inj.submit(static_cast<std::uint64_t>(i), i * 0.1, sentence(i));
        const auto r = inj.release(i * 0.1);
        if (!r.empty()) groupSizes.push_back(r.size());
    }
    const auto tail = inj.drain();

    std::size_t total = tail.size();
    for (const std::size_t n : groupSizes) {
        EXPECT_GE(n, 3u);
        EXPECT_LE(n, 8u);
        total += n;
    }
    EXPECT_EQ(total, 300u);
    EXPECT_GT(groupSizes.size(), 20u);
}

// ------------------------------------------------------------------ drain

TEST(Injector, DrainFlushesHeldReorderedDelayedAndBurstMessages) {
    Config c = none();
    c.pReorder = 0.2;
    c.pDelay = 0.2;
    c.pBurst = 0.2;
    Injector inj(11, c);
    std::set<std::uint64_t> seen;
    for (int i = 0; i < 40; ++i) {
        inj.submit(static_cast<std::uint64_t>(i), i * 0.1, sentence(i));
        for (const Outgoing& o : inj.release(i * 0.1)) seen.insert(o.seq);
    }
    for (const Outgoing& o : inj.drain()) seen.insert(o.seq);
    EXPECT_EQ(seen.size(), 40u);
    EXPECT_TRUE(inj.drain().empty());
}

// ------------------------------------------------------------------ mixed faults, determinism

TEST(Injector, MixedFaultsLoseNothingAndKeepEmitIndexesContiguous) {
    const auto v = run(42, Config{}, 5000);   // default probabilities: every fault on
    std::map<std::uint64_t, int> n;
    for (const Outgoing& o : v) ++n[o.seq];
    ASSERT_EQ(n.size(), 5000u);
    for (const auto& [seq, count] : n) EXPECT_TRUE(count == 1 || count == 2) << seq;

    std::uint64_t expected = 0;
    std::set<Fault> kinds;
    for (const Outgoing& o : v) {
        kinds.insert(o.fault);
        if (!o.dropped) { EXPECT_EQ(o.emitIndex, expected++); }
    }
    EXPECT_EQ(kinds.size(), 8u);   // None plus all seven faults showed up
}

TEST(Injector, SameSeedGivesTheSameStreamAndADifferentSeedDoesNot) {
    const auto a = run(42, Config{}, 3000), b = run(42, Config{}, 3000), d = run(43, Config{}, 3000);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].seq, b[i].seq);
        EXPECT_EQ(a[i].copy, b[i].copy);
        EXPECT_EQ(a[i].fault, b[i].fault);
        EXPECT_EQ(a[i].dropped, b[i].dropped);
        EXPECT_EQ(a[i].text, b[i].text);
        EXPECT_EQ(a[i].emitIndex, b[i].emitIndex);
    }
    bool differs = a.size() != d.size();
    for (std::size_t i = 0; !differs && i < a.size(); ++i) differs = a[i].text != d[i].text;
    EXPECT_TRUE(differs);
}

TEST(Injector, FaultChoicePerSentenceDoesNotDependOnWhenReleaseIsCalled) {
    // Same seed, but one run polls release() every tick and the other only drains at the end.
    const auto polled = run(42, Config{}, 2000);
    Injector lazy(42, Config{});
    for (int i = 0; i < 2000; ++i) lazy.submit(static_cast<std::uint64_t>(i), i * 0.1, sentence(i));
    const auto drained = lazy.drain();

    std::map<std::uint64_t, Fault> a, b;
    for (const Outgoing& o : polled)
        if (o.copy == 0 && o.fault != Fault::Burst) a[o.seq] = o.fault;
    for (const Outgoing& o : drained)
        if (o.copy == 0 && o.fault != Fault::Burst) b[o.seq] = o.fault;
    EXPECT_EQ(a, b);
}

TEST(Injector, ObservedFaultRatesMatchTheConfiguredProbabilities) {
    Config c = none();
    c.pDrop = 0.1;
    c.pBadChecksum = 0.2;
    int drops = 0, bad = 0;
    for (const Outgoing& o : run(77, c, 20000)) {
        drops += o.fault == Fault::Drop;
        bad += o.fault == Fault::BadChecksum;
    }
    EXPECT_NEAR(drops / 20000.0, 0.1, 0.01);
    EXPECT_NEAR(bad / 20000.0, 0.2, 0.01);
}

// ------------------------------------------------------------------ config validation

TEST(Injector, RejectsInvalidConfiguration) {
    Config c = none();
    c.pDrop = -0.1;
    EXPECT_TRUE(throwsInvalid(c));

    c = none();
    c.pDrop = 0.6;
    c.pDelay = 0.6;
    EXPECT_TRUE(throwsInvalid(c));            // sums past 1

    c = none();
    c.pDrop = std::nan("");
    EXPECT_TRUE(throwsInvalid(c));

    c = none();
    c.maxReorder = 0;
    EXPECT_TRUE(throwsInvalid(c));

    c = none();
    c.delayMsMin = 500;
    c.delayMsMax = 100;
    EXPECT_TRUE(throwsInvalid(c));

    c = none();
    c.burstMin = 1;
    EXPECT_TRUE(throwsInvalid(c));

    c = none();
    c.burstMin = 5;
    c.burstMax = 4;
    EXPECT_TRUE(throwsInvalid(c));

    EXPECT_FALSE(throwsInvalid(Config{}));
    EXPECT_FALSE(throwsInvalid(none()));
}

TEST(Injector, FaultNamesAreDistinctAndStable) {
    std::set<std::string> names;
    for (const Fault f : {Fault::None, Fault::BadChecksum, Fault::Truncate, Fault::Duplicate,
                          Fault::Reorder, Fault::Drop, Fault::Delay, Fault::Burst})
        names.insert(faultName(f));
    EXPECT_EQ(names.size(), 8u);
    EXPECT_STREQ(faultName(Fault::None), "none");
    EXPECT_STREQ(faultName(Fault::BadChecksum), "bad_checksum");
}
