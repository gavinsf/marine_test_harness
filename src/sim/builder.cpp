// https://aprs.gids.nl/nmea/
#include "builder.hpp"
#include "checksum.hpp"
#include <initializer_list>

std::string join(std::initializer_list<std::string> fields) {
    std::string out;
    bool first = true;
    for (const auto& f : fields) {
        if (!first) out += ',';
        out += f;
        first = false;
    }
    return out;
}

std::string frame(const std::string& body) {
    char tail[8];
    std::snprintf(tail, sizeof tail, "*%02X\r\n", checksum(body));
    return "$" + body + tail;
}

std::string hemiNS(double lat) { return lat >= 0 ? "N" : "S"; }
std::string hemiEW(double lon) { return lon >= 0 ? "E" : "W"; }

std::string gga(const ShipState& s) {
    return frame(join({"GPGGA", utcTime(s), fmtLat(s.lat), hemiNS(s.lat),
                       fmtLon(s.lon), hemiEW(s.lon), "1", "08", "0.9",
                       "0.0", "M", "", "M", "", ""}));
}

std::string rmc(const ShipState& s) {
    return frame(join({"GPRMC", utcTime(s), "A", fmtLat(s.lat), hemiNS(s.lat),
                       fmtLon(s.lon), hemiEW(s.lon), fmtFixed(s.sog, 1),
                       fmtFixed(s.cog, 1), utcDate(s), "", ""}));
}

std::string hdt(const ShipState& s) {
    return frame(join({"HEHDT", fmtFixed(s.heading, 1), "T"}));
}

std::string dpt(const ShipState& s) {
    return frame(join({"SDDPT", fmtFixed(s.depth, 1), "0.0"}));
}

