// injector.cpp
#include "sim/injector.hpp"

#include <cstdio>
#include <limits>
#include <random>
#include <stdexcept>

namespace {

// One RNG per message: same seed and seq always give the same draws.
std::mt19937_64 rngFor(std::uint64_t seed, std::uint64_t seq) {
    return std::mt19937_64(seed ^ (seq * 0x9E3779B97F4A7C15ULL));
}

double uniform(std::mt19937_64& rng) { return (rng() >> 11) * (1.0 / 9007199254740992.0); }

int between(std::mt19937_64& rng, int lo, int hi) {
    return lo + static_cast<int>(rng() % static_cast<std::uint64_t>(hi - lo + 1));
}

// Swap the two hex digits after '*' for a value that is guaranteed wrong.
std::string badChecksum(std::string s, std::mt19937_64& rng) {
    const std::size_t star = s.rfind('*');
    if (star == std::string::npos || s.size() < star + 3) return s;
    unsigned truth = 0;
    for (std::size_t i = 1; i < star; ++i) truth ^= static_cast<unsigned char>(s[i]);
    unsigned wrong = truth;
    while (wrong == truth) wrong = static_cast<unsigned>(rng() & 0xFF);
    char hex[3];
    std::snprintf(hex, sizeof hex, "%02X", wrong);
    s.replace(star + 1, 2, hex);
    return s;
}

// Cut the sentence short but keep the CRLF so line-based parsers still see a bad line.
std::string truncated(const std::string& s, std::mt19937_64& rng) {
    std::size_t end = s.find_first_of("\r\n");
    if (end == std::string::npos) end = s.size();
    if (end < 3) return s;
    const std::size_t keep = 1 + rng() % (end - 1);   // 1..end-1 characters
    return s.substr(0, keep) + s.substr(end);
}

Outgoing makeOutgoing(std::uint64_t seq, int copy, Fault fault, const std::string& original,
                      const std::string& text) {
    Outgoing o;
    o.seq = seq;
    o.copy = copy;
    o.fault = fault;
    o.original = original;
    o.text = text;
    return o;
}

}  // namespace

const char* faultName(Fault f) {
    switch (f) {
        case Fault::None:        return "none";
        case Fault::BadChecksum: return "bad_checksum";
        case Fault::Truncate:    return "truncate";
        case Fault::Duplicate:   return "duplicate";
        case Fault::Reorder:     return "reorder";
        case Fault::Drop:        return "drop";
        case Fault::Delay:       return "delay";
        case Fault::Burst:       return "burst";
    }
    return "?";
}

Injector::Injector(std::uint64_t seed, const Config& cfg) : seed_(seed), cfg_(cfg) {
    const double sum = cfg.pBadChecksum + cfg.pTruncate + cfg.pDuplicate + cfg.pReorder +
                       cfg.pDrop + cfg.pDelay + cfg.pBurst;
    for (double p : {cfg.pBadChecksum, cfg.pTruncate, cfg.pDuplicate, cfg.pReorder, cfg.pDrop,
                     cfg.pDelay, cfg.pBurst})
        if (!(p >= 0.0 && p <= 1.0)) throw std::invalid_argument("fault probability must be in [0, 1]");
    if (sum > 1.0) throw std::invalid_argument("fault probabilities sum to more than 1");
    if (cfg.maxReorder < 1) throw std::invalid_argument("maxReorder must be >= 1");
    if (cfg.delayMsMin < 0 || cfg.delayMsMax < cfg.delayMsMin)
        throw std::invalid_argument("delay range must satisfy 0 <= delayMsMin <= delayMsMax");
    if (cfg.burstMin < 2 || cfg.burstMax < cfg.burstMin)
        throw std::invalid_argument("burst range must satisfy 2 <= burstMin <= burstMax");
}

Fault Injector::pickFault(double roll) const {
    double acc = 0;
    if (roll < (acc += cfg_.pBadChecksum)) return Fault::BadChecksum;
    if (roll < (acc += cfg_.pTruncate))    return Fault::Truncate;
    if (roll < (acc += cfg_.pDuplicate))   return Fault::Duplicate;
    if (roll < (acc += cfg_.pReorder))     return Fault::Reorder;
    if (roll < (acc += cfg_.pDrop))        return Fault::Drop;
    if (roll < (acc += cfg_.pDelay))       return Fault::Delay;
    if (roll < (acc += cfg_.pBurst))       return Fault::Burst;
    return Fault::None;
}

void Injector::schedule(Outgoing o, double due) {
    queue_[{due, order_++}] = std::move(o);
}

void Injector::flushBurst(double t) {
    for (Outgoing& o : burst_) schedule(std::move(o), t);
    burst_.clear();
}

void Injector::releaseHeld(std::uint64_t seq, double t) {
    for (std::size_t i = 0; i < held_.size();) {
        if (held_[i].releaseSeq <= seq) {
            schedule(std::move(held_[i].out), t);
            held_.erase(held_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void Injector::submit(std::uint64_t seq, double t, const std::string& s) {
    lastT_ = t;
    std::mt19937_64 rng = rngFor(seed_, seq);

    if (burstLeft_ > 0) {                       // inside a burst: hold, no new fault roll
        burst_.push_back(makeOutgoing(seq, 0, Fault::Burst, s, s));
        if (--burstLeft_ == 0) flushBurst(t);
    } else {
        switch (pickFault(uniform(rng))) {
            case Fault::None:
                schedule(makeOutgoing(seq, 0, Fault::None, s, s), t);
                break;
            case Fault::BadChecksum:
                schedule(makeOutgoing(seq, 0, Fault::BadChecksum, s, badChecksum(s, rng)), t);
                break;
            case Fault::Truncate:
                schedule(makeOutgoing(seq, 0, Fault::Truncate, s, truncated(s, rng)), t);
                break;
            case Fault::Duplicate:
                schedule(makeOutgoing(seq, 0, Fault::Duplicate, s, s), t);
                schedule(makeOutgoing(seq, 1, Fault::Duplicate, s, s), t + between(rng, 1, 500) / 1000.0);
                break;
            case Fault::Reorder:
                held_.push_back({makeOutgoing(seq, 0, Fault::Reorder, s, s),
                                 seq + static_cast<std::uint64_t>(between(rng, 1, cfg_.maxReorder))});
                break;
            case Fault::Drop: {
                Outgoing o = makeOutgoing(seq, 0, Fault::Drop, s, "");
                o.dropped = true;
                schedule(std::move(o), t);      // comes out of release() so it gets logged
                break;
            }
            case Fault::Delay:
                schedule(makeOutgoing(seq, 0, Fault::Delay, s, s),
                         t + between(rng, cfg_.delayMsMin, cfg_.delayMsMax) / 1000.0);
                break;
            case Fault::Burst:
                burstLeft_ = between(rng, cfg_.burstMin, cfg_.burstMax) - 1;
                burst_.push_back(makeOutgoing(seq, 0, Fault::Burst, s, s));
                break;
        }
    }
    releaseHeld(seq, t);   // reordered messages whose k later messages have now arrived
}

std::vector<Outgoing> Injector::release(double now) {
    std::vector<Outgoing> out;
    while (!queue_.empty() && queue_.begin()->first.first <= now) {
        Outgoing o = std::move(queue_.begin()->second);
        queue_.erase(queue_.begin());
        if (!o.dropped) o.emitIndex = emitted_++;
        out.push_back(std::move(o));
    }
    return out;
}

std::vector<Outgoing> Injector::drain() {
    for (Held& h : held_) schedule(std::move(h.out), lastT_);
    held_.clear();
    burstLeft_ = 0;
    flushBurst(lastT_);
    return release(std::numeric_limits<double>::infinity());
}