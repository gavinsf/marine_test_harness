#include "checksum.hpp"

uint8_t checksum(std::string_view body) {
    uint8_t cs = 0;
    for (char c : body) {
        cs ^= static_cast<uint8_t>(c);
    }
    return cs;
}


std::string utcTime(const ShipState& s) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d%02d%02d.%02d", s.hour, s.minute,
                  s.second, s.centisecond);
    return buf;
}

std::string utcDate(const ShipState& s) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d%02d%02d",
                  s.utc.tm_mday, s.utc.tm_mon + 1, (s.utc.tm_year + 1900) % 100);
    return buf;
}