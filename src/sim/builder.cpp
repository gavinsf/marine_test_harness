// https://aprs.gids.nl/nmea/
#include "sim/builder.hpp"
#include "common/checksum.hpp"
#include "common/utc.hpp"
#include <initializer_list>
#include <cmath>
#include <format>

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

std::string fmtLat(double lat) {
     lat = std::fabs(lat);
    int deg = static_cast<int>(lat);
    double min = (lat - deg) * 60.0;
    if (min >= 59.99995) { min = 0.0; ++deg; }   // avoid "60.0000"
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d%07.4f", deg, min);
    return buf;
}

std::string fmtLon(double lon) {            // dddmm.mmmm
    lon = std::fabs(lon);
    int deg = static_cast<int>(lon);
    double min = (lon - deg) * 60.0;
    if (min >= 59.99995) { min = 0.0; ++deg; }
    char buf[16];
    std::snprintf(buf, sizeof buf, "%03d%07.4f", deg, min);
    return buf;
}

std::string fmtFixed(double v, int decimals) {
    return std::format("{:.{}f}", v, decimals);
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

// Wind speed and angle: $WIMWV,<angle>,R,<speed>,N,A  (relative, knots, valid)
std::string mwv(const ShipState& s) {
    return frame(join({"WIMWV", fmtFixed(s.wind_angle, 1), "R",
                       fmtFixed(s.wind_speed, 1), "N", "A"}));
}
