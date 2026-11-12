#pragma once
#include <fstream>
#include <string>

#include "sim/injector.hpp"

// One JSON object (no trailing newline) describing a message the injector produced:
//   {"seq":12,"copy":0,"emit":9,"t":1.200,"fault":"none","dropped":false,
//    "original":"$GPGGA...*5A\r\n","text":"$GPGGA...*5A\r\n"}
// "seq"      index of the sentence the simulator generated (0, 1, 2, ...)
// "copy"     0 normally, 1 for the extra copy made by a duplicate fault
// "emit"     position in the stream actually sent, or null for a dropped message
// "t"        simulated seconds since start at which it was sent (or would have been)
// "original" the valid sentence; "text" is what went on the wire ("" if dropped)
// Control and non-ASCII bytes are written as \u00XX, so the line is always valid JSON.
std::string toJsonLine(const Outgoing& o, double t);

// Ground-truth log (REQ-008): one JSON line per message, dropped ones included.
// An empty path disables logging, so callers can write unconditionally.
class GroundTruthLog {
    public:
        explicit GroundTruthLog(const std::string& path);   // throws std::runtime_error if it cannot be opened
        void write(const Outgoing& o, double t);
        bool enabled() const { return out_.is_open(); }

    private:
        std::ofstream out_;
};
