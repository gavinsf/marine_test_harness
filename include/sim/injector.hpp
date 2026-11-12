#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

enum class Fault { None, BadChecksum, Truncate, Duplicate, Reorder, Drop, Delay, Burst };


struct Config {
    double pBadChecksum = 0.05;
    double pTruncate = 0.03;
    double pDuplicate = 0.03;
    double pReorder = 0.04;
    double pDrop = 0.05;
    double pDelay = 0.03;
    double pBurst = 0.01;
    int maxReorder = 3;                 // later messages a reordered one slips behind
    int delayMsMin = 200, delayMsMax = 3000;
    int burstMin = 3, burstMax = 8;     // burstMin must be >= 2
};

const char* faultName(Fault f);         // "none", "bad_checksum", ... for the ground-truth log

struct Outgoing {
    std::uint64_t seq = 0;
    int copy = 0;                       // 1 for the duplicate copy
    Fault fault = Fault::None;
    bool dropped = false;               // log it, do not send it
    std::string original;
    std::string text;                   // what goes on the wire
    std::uint64_t emitIndex = 0;        // position in the sent stream (unset for drops)
};

class Injector {
    public:
        Injector(std::uint64_t seed, const Config& cfg);
        void submit(std::uint64_t seq, double t, const std::string& sentence);
        std::vector<Outgoing> release(double now);
        std::vector<Outgoing> drain();
    
    private:
        struct Held {
            Outgoing out;
            std::uint64_t releaseSeq;
        };

        void schedule(Outgoing o, double due);
        void flushBurst(double t);
        void releaseHeld(std::uint64_t seq, double t);
        Fault pickFault(double roll) const;

        std::uint64_t seed_;
        Config cfg_;
        std::map<std::pair<double, std::uint64_t>, Outgoing> queue_;  // key: (due, insertion order)
        std::vector<Held> held_;            // reordered messages waiting for later ones
        std::vector<Outgoing> burst_;       // burst being collected
        int burstLeft_ = 0;
        std::uint64_t order_ = 0;
        std::uint64_t emitted_ = 0;
        double lastT_ = 0;
};