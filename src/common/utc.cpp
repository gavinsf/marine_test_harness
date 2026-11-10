#include "common/utc.hpp"
#include "common/ship_state.hpp"

std::string utcTime(const ShipState& s) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d%02d%02d.%02d", s.utc.tm_hour, s.utc.tm_min,
                  s.utc.tm_sec, s.centis);
    return buf;
}

std::string utcDate(const ShipState& s) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d%02d%02d",
                  s.utc.tm_mday, s.utc.tm_mon + 1, (s.utc.tm_year + 1900) % 100);
    return buf;
}