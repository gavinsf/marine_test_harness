#include "sim/parser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

#include "common/checksum.hpp"

const char* to_string(ParseError e) {
    switch (e) {
        case ParseError::Malformed:           return "Malformed";
        case ParseError::BadChecksum:         return "BadChecksum";
        case ParseError::WrongFieldCount:     return "WrongFieldCount";
        case ParseError::InvalidField:        return "InvalidField";
        case ParseError::UnsupportedSentence: return "UnsupportedSentence";
        case ParseError::TooLong:             return "TooLong";
    }
    return "Unknown";
}

namespace {

using Fields = std::vector<std::string_view>;

std::string_view strip_eol(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

bool is_start(char c) { return c == '$' || c == '!'; }
bool all_digits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
int to_int(std::string_view digits) {
    int v = 0;
    std::from_chars(digits.data(), digits.data() + digits.size(), v);
    return v;
}

// Plain decimal: optional '-', digits, at most one '.'. Rejects the "inf", "nan" and
// exponent forms that std::from_chars would otherwise accept.
bool strict_double(std::string_view s, double& out) {
    std::size_t i = (!s.empty() && s[0] == '-') ? 1 : 0;
    bool digits = false, dot = false;
    for (; i < s.size(); ++i) {
        if (s[i] >= '0' && s[i] <= '9') digits = true;
        else if (s[i] == '.' && !dot) dot = true;
        else return false;
    }
    if (!digits) return false;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size() && std::isfinite(out);
}

// Per-field validation. Every method returns false on failure and records which
// field was wrong in `bad`; empty fields succeed and leave the destination nullopt.
struct Validator {
    std::string bad;

    bool fail(std::string_view name, std::string_view value, std::string_view why) {
        bad = std::string(name) + " '" + std::string(value) + "': " + std::string(why);
        return false;
    }

    bool number(std::string_view v, std::string_view name, double lo, double hi, std::optional<double>& dst) {
        if (v.empty()) return true;
        double d = 0;
        if (!strict_double(v, d)) return fail(name, v, "not a number");
        if (d < lo || d > hi) return fail(name, v, "out of range");
        dst = d;
        return true;
    }

    bool integer(std::string_view v, std::string_view name, int lo, int hi, std::optional<int>& dst) {
        if (v.empty()) return true;
        if (!all_digits(v) || v.size() > 6) return fail(name, v, "not an integer");
        int n = to_int(v);
        if (n < lo || n > hi) return fail(name, v, "out of range");
        dst = n;
        return true;
    }

    bool one_of(std::string_view v, std::string_view name, std::string_view allowed, std::optional<char>& dst) {
        if (v.empty()) return true;
        if (v.size() != 1 || allowed.find(v[0]) == std::string_view::npos) {
            return fail(name, v, "expected one of '" + std::string(allowed) + "'");
        }
        dst = v[0];
        return true;
    }

    // A unit/indicator field is optional on its own, but a value must not lack it.
    bool unit(std::string_view v, std::string_view name, std::string_view allowed, bool have_value) {
        std::optional<char> ignored;
        if (!one_of(v, name, allowed, ignored)) return false;
        if (have_value && v.empty()) return fail(name, v, "value present but unit missing");
        return true;
    }

    // hhmmss or hhmmss.s+
    bool time(std::string_view v, std::optional<UtcTime>& dst) {
        if (v.empty()) return true;
        auto dot = v.find('.');
        auto whole = v.substr(0, dot);
        if (whole.size() != 6 || !all_digits(whole)) return fail("time", v, "expected hhmmss[.ss]");
        UtcTime t;
        t.hour = to_int(whole.substr(0, 2));
        t.minute = to_int(whole.substr(2, 2));
        int sec = to_int(whole.substr(4, 2));
        if (t.hour > 23 || t.minute > 59 || sec > 59) return fail("time", v, "out of range");
        t.second = sec;
        if (dot != std::string_view::npos) {
            auto frac = v.substr(dot + 1);
            if (!all_digits(frac)) return fail("time", v, "bad fractional seconds");
            double f = 0;
            strict_double("0." + std::string(frac), f);
            t.second += f;
        }
        dst = t;
        return true;
    }

    // ddmmyy
    bool date(std::string_view v, std::optional<UtcDate>& dst) {
        if (v.empty()) return true;
        if (v.size() != 6 || !all_digits(v)) return fail("date", v, "expected ddmmyy");
        UtcDate d;
        d.day = to_int(v.substr(0, 2));
        d.month = to_int(v.substr(2, 2));
        d.year = to_int(v.substr(4, 2));
        static constexpr std::array<int, 12> kDays = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (d.month < 1 || d.month > 12 || d.day < 1 || d.day > kDays[d.month - 1]) {
            return fail("date", v, "not a calendar date");
        }
        if (d.month == 2 && d.day == 29 && d.year % 4 != 0) return fail("date", v, "not a leap year");
        dst = d;
        return true;
    }

    // ddmm.mmmm (deg_digits = 2) or dddmm.mmmm (3) plus a hemisphere letter, giving signed
    // decimal degrees. Value and hemisphere must be both present or both empty.
    bool coord(std::string_view value, std::string_view hemi, std::string_view name, int deg_digits,
               int max_deg, char positive, char negative, std::optional<double>& dst) {
        if (value.empty() && hemi.empty()) return true;
        if (value.empty()) return fail(name, hemi, "hemisphere present but value empty");
        if (hemi.empty()) return fail(name, value, "value present but hemisphere empty");
        if (hemi.size() != 1 || (hemi[0] != positive && hemi[0] != negative)) {
            return fail(name, hemi, std::string("hemisphere must be ") + positive + " or " + negative);
        }
        auto dot = value.find('.');
        auto whole = value.substr(0, dot);
        if (whole.size() != static_cast<std::size_t>(deg_digits + 2) || !all_digits(whole)) {
            return fail(name, value, "wrong digit layout");
        }
        if (dot != std::string_view::npos && !all_digits(value.substr(dot + 1))) {
            return fail(name, value, "bad fractional minutes");
        }
        int deg = to_int(whole.substr(0, deg_digits));
        double minutes = 0;
        strict_double(value.substr(deg_digits), minutes);
        if (minutes >= 60.0) return fail(name, value, "minutes must be below 60");
        if (deg > max_deg || (deg == max_deg && minutes > 0.0)) return fail(name, value, "degrees out of range");
        double result = deg + minutes / 60.0;
        dst = hemi[0] == negative ? -result : result;
        return true;
    }

    bool lat(std::string_view v, std::string_view h, std::optional<double>& dst) {
        return coord(v, h, "latitude", 2, 90, 'N', 'S', dst);
    }
    bool lon(std::string_view v, std::string_view h, std::optional<double>& dst) {
        return coord(v, h, "longitude", 3, 180, 'E', 'W', dst);
    }

    // ---- one method per sentence; f[0] is the address field, data starts at f[1] ----

    bool gga(const std::string& talker, const Fields& f, Gga& g) {
        g.talker = talker;
        return time(f[1], g.time) &&
               lat(f[2], f[3], g.lat) &&
               lon(f[4], f[5], g.lon) &&
               integer(f[6], "fix quality", 0, 8, g.fix_quality) &&
               integer(f[7], "satellites", 0, 99, g.satellites) &&
               number(f[8], "hdop", 0.0, 99.99, g.hdop) &&
               number(f[9], "altitude", -1000.0, 20000.0, g.altitude_m) &&
               unit(f[10], "altitude unit", "M", g.altitude_m.has_value()) &&
               number(f[11], "geoid separation", -1000.0, 1000.0, g.geoid_separation_m) &&
               unit(f[12], "geoid unit", "M", g.geoid_separation_m.has_value()) &&
               number(f[13], "dgps age", 0.0, 99999.0, g.dgps_age_s) &&
               integer(f[14], "dgps station", 0, 1023, g.dgps_station);
    }

    bool rmc(const std::string& talker, const Fields& f, Rmc& r) {
        r.talker = talker;
        std::optional<double> variation;
        std::optional<char> variation_dir;
        bool ok = time(f[1], r.time) &&
                  one_of(f[2], "status", "AV", r.status) &&
                  lat(f[3], f[4], r.lat) &&
                  lon(f[5], f[6], r.lon) &&
                  number(f[7], "speed over ground", 0.0, 999.9, r.sog_knots) &&
                  number(f[8], "course over ground", 0.0, 360.0, r.cog_deg) &&
                  date(f[9], r.date) &&
                  number(f[10], "magnetic variation", 0.0, 180.0, variation) &&
                  one_of(f[11], "magnetic variation direction", "EW", variation_dir) &&
                  (f.size() < 13 || one_of(f[12], "mode", "ADEFMNPRS", r.mode));
        if (!ok) return false;
        if (variation && !variation_dir) return fail("magnetic variation direction", "", "value present but direction missing");
        if (variation) r.magnetic_variation_deg = (*variation_dir == 'W') ? -*variation : *variation;
        return true;
    }

    bool hdt(const std::string& talker, const Fields& f, Hdt& h) {
        h.talker = talker;
        return number(f[1], "heading", 0.0, 360.0, h.heading_true_deg) &&
               unit(f[2], "heading reference", "T", h.heading_true_deg.has_value());
    }

    bool dpt(const std::string& talker, const Fields& f, Dpt& d) {
        d.talker = talker;
        return number(f[1], "depth", 0.0, 11000.0, d.depth_m) &&
               number(f[2], "transducer offset", -100.0, 100.0, d.offset_m) &&
               (f.size() < 4 || number(f[3], "max range", 0.0, 11000.0, d.max_range_m));
    }

    bool mwv(const std::string& talker, const Fields& f, Mwv& m) {
        m.talker = talker;
        return number(f[1], "wind angle", 0.0, 360.0, m.angle_deg) &&
               one_of(f[2], "wind reference", "RT", m.reference) &&
               (!m.angle_deg || m.reference || fail("wind reference", "", "value present but reference missing")) &&
               number(f[3], "wind speed", 0.0, 500.0, m.speed) &&
               one_of(f[4], "wind speed unit", "KMNS", m.speed_units) &&
               (!m.speed || m.speed_units || fail("wind speed unit", "", "value present but unit missing")) &&
               one_of(f[5], "status", "AV", m.status);
    }
};

struct Rule {
    std::string_view type;
    std::size_t min, max;     // allowed field counts, including the address field
};

constexpr std::array<Rule, 5> kRules = {{
    {"GGA", 15, 15},
    {"RMC", 12, 13},   // 13 with the NMEA 2.3 mode indicator
    {"HDT",  3,  3},
    {"DPT",  3,  4},   // 4 with max range
    {"MWV",  6,  6},
}};

bool is_address_char(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

}  // namespace

std::string_view typeOf(const Sentence& s) { return kRules[s.index()].type; }

const std::string& talkerOf(const Sentence& s) {
    return std::visit([](const auto& v) -> const std::string& { return v.talker; }, s);
}

std::optional<ParseError> NMEAParser::fail(ParseError e, std::string detail) {
    detail_ = std::move(detail);
    return e;
}

std::optional<ParseError> NMEAParser::parse(std::string_view frame, Sentence& out) {
    detail_.clear();
    if (auto err = frame_check(frame)) return err;
    if (auto err = checksum_check(frame)) return err;
    Fields fields;
    if (auto err = get_fields(frame, fields)) return err;
    if (auto err = field_count_check(fields)) return err;
    return format_valid(fields, out);
}

std::optional<ParseError> NMEAParser::parse(std::string_view frame) {
    Sentence ignored;
    return parse(frame, ignored);
}

// Stage 1: start character, length, exactly one '*' followed by exactly two hex digits.
std::optional<ParseError> NMEAParser::frame_check(std::string_view frame) {
    frame = strip_eol(frame);
    if (frame.empty()) return fail(ParseError::Malformed, "empty");
    if (!is_start(frame.front())) return fail(ParseError::Malformed, "does not start with '$' or '!'");
    if (frame.size() > kMaxAcceptedLength) {
        return fail(ParseError::TooLong, std::to_string(frame.size()) + " characters");
    }

    const std::size_t star = frame.find('*');
    if (star == std::string_view::npos) return fail(ParseError::Malformed, "no '*' checksum delimiter");
    const std::string_view tail = frame.substr(star + 1);
    if (tail.size() != 2 || !is_hex_char(tail[0]) || !is_hex_char(tail[1])) {
        return fail(ParseError::Malformed, "'*' must be followed by exactly two hex digits, got '" + std::string(tail) + "'");
    }

    // Everything between the start char and '*' must be printable ASCII with no stray delimiters.
    for (char c : frame.substr(1, star - 1)) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u > 0x7E || c == '$' || c == '!' || c == '*') {
            return fail(ParseError::Malformed, "illegal character in sentence body");
        }
    }
    return std::nullopt;
}

// Stage 2: XOR of the body against the two hex digits after '*' (either case).
std::optional<ParseError> NMEAParser::checksum_check(std::string_view frame) {
    frame = strip_eol(frame);
    const std::size_t star = frame.rfind('*');
    if (frame.empty() || !is_start(frame.front()) || star == std::string_view::npos || frame.size() != star + 3) {
        return fail(ParseError::Malformed, "cannot locate checksum");
    }
    const int hi = hex_digit(frame[star + 1]);
    const int lo = hex_digit(frame[star + 2]);
    if (hi < 0 || lo < 0) return fail(ParseError::Malformed, "checksum is not two hex digits");

    const std::string_view body = frame.substr(1, star - 1);
    if (checksum(body) != hi * 16 + lo) return fail(ParseError::BadChecksum, "checksum mismatch");
    return std::nullopt;
}

// Stage 3: split the body on ',' keeping empty fields. fields[0] is the address (e.g. GPGGA).
std::optional<ParseError> NMEAParser::get_fields(std::string_view frame, std::vector<std::string_view>& out) {
    out.clear();
    frame = strip_eol(frame);
    const std::size_t star = frame.rfind('*');
    if (frame.empty() || !is_start(frame.front()) || star == std::string_view::npos) {
        return fail(ParseError::Malformed, "cannot locate sentence body");
    }

    const std::string_view body = frame.substr(1, star - 1);
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = body.find(',', start);
        if (pos == std::string_view::npos) {
            out.push_back(body.substr(start));
            break;
        }
        out.push_back(body.substr(start, pos - start));
        start = pos + 1;
    }
    return std::nullopt;
}

// Stage 4: split the address into talker + type, look the type up, check the field count.
std::optional<ParseError> NMEAParser::field_count_check(const std::vector<std::string_view>& fields) {
    if (fields.empty()) return fail(ParseError::Malformed, "no fields");

    const std::string_view address = fields[0];
    if (address.size() != 5 || !std::all_of(address.begin(), address.end(), is_address_char)) {
        if (!address.empty() && address[0] == 'P') {
            return fail(ParseError::UnsupportedSentence, "proprietary sentence " + std::string(address));
        }
        return fail(ParseError::Malformed, "address '" + std::string(address) + "' is not <talker:2><type:3>");
    }

    const std::string_view type = address.substr(2);
    for (const Rule& r : kRules) {
        if (r.type != type) continue;
        if (fields.size() < r.min || fields.size() > r.max) {
            return fail(ParseError::WrongFieldCount,
                        std::string(type) + " has " + std::to_string(fields.size()) + " fields, expected " +
                        std::to_string(r.min) + (r.min == r.max ? "" : "-" + std::to_string(r.max)));
        }
        return std::nullopt;
    }
    return fail(ParseError::UnsupportedSentence, std::string(type));
}

// Stage 5 + 6: validate every field and build the typed result.
std::optional<ParseError> NMEAParser::format_valid(const std::vector<std::string_view>& fields, Sentence& out) {
    if (auto err = field_count_check(fields)) return err;   // keeps the field indexing below safe

    const std::string talker(fields[0].substr(0, 2));
    const std::string_view type = fields[0].substr(2);

    Validator v;
    bool ok = false;
    if (type == "GGA")      { Gga s; ok = v.gga(talker, fields, s); if (ok) out = std::move(s); }
    else if (type == "RMC") { Rmc s; ok = v.rmc(talker, fields, s); if (ok) out = std::move(s); }
    else if (type == "HDT") { Hdt s; ok = v.hdt(talker, fields, s); if (ok) out = std::move(s); }
    else if (type == "DPT") { Dpt s; ok = v.dpt(talker, fields, s); if (ok) out = std::move(s); }
    else                    { Mwv s; ok = v.mwv(talker, fields, s); if (ok) out = std::move(s); }

    if (!ok) return fail(ParseError::InvalidField, v.bad);
    return std::nullopt;
}
