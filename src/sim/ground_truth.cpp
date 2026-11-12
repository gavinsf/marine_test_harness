#include "sim/ground_truth.hpp"

#include <cstdio>
#include <stdexcept>

namespace {

void appendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (const unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\r': out += "\\r";  break;
            case '\n': out += "\\n";  break;
            default:
                if (c < 0x20 || c >= 0x7F) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04X", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

}  // namespace

std::string toJsonLine(const Outgoing& o, double t) {
    char num[64];
    std::string s = "{\"seq\":" + std::to_string(o.seq) + ",\"copy\":" + std::to_string(o.copy) + ",\"emit\":";
    s += o.dropped ? "null" : std::to_string(o.emitIndex);
    std::snprintf(num, sizeof num, "%.3f", t);
    s += ",\"t\":";
    s += num;
    s += ",\"fault\":\"";
    s += faultName(o.fault);
    s += "\",\"dropped\":";
    s += o.dropped ? "true" : "false";
    s += ",\"original\":";
    appendEscaped(s, o.original);
    s += ",\"text\":";
    appendEscaped(s, o.text);
    s += '}';
    return s;
}

GroundTruthLog::GroundTruthLog(const std::string& path) {
    if (path.empty()) return;
    out_.open(path, std::ios::out | std::ios::trunc);
    if (!out_) throw std::runtime_error("cannot open ground-truth log '" + path + "'");
}

void GroundTruthLog::write(const Outgoing& o, double t) {
    if (!out_.is_open()) return;
    out_ << toJsonLine(o, t) << '\n';
    out_.flush();   // keep the log intact even if the simulator is killed
}
