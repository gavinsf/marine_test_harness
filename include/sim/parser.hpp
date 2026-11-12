#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// NMEA 0183 sentence parser.
//
// Pipeline (each stage fails early with its own error):
//   frame_check        starts with '$' (or '!'), '*' + exactly two hex digits, length limit
//   checksum_check     XOR of the bytes between the start char and '*'
//   get_fields         split on ',', empty fields preserved
//   field_count_check  sentence type lookup, then field count for that type
//   format_valid       per-field formats and ranges; empty optional fields become nullopt
// parse() runs them in that order and returns the typed result.

enum class ParseError {
    Malformed,            // bad frame: missing/short/non-hex checksum, no start char, bad characters, bad address
    BadChecksum,          // declared checksum does not match the computed one
    WrongFieldCount,      // field count is wrong for this sentence type
    InvalidField,         // a field failed its format or range check
    UnsupportedSentence,  // well formed, but not a sentence type we decode
    TooLong,              // longer than kMaxSentenceLength + kLengthSlack
};

const char* to_string(ParseError e);

inline constexpr std::size_t kMaxSentenceLength = 82;   // per spec
inline constexpr std::size_t kLengthSlack = 8;          // tolerance for sloppy talkers
inline constexpr std::size_t kMaxAcceptedLength = kMaxSentenceLength + kLengthSlack;

struct UtcTime {
    int hour = 0;
    int minute = 0;
    double second = 0.0;   // includes any fractional part
};

struct UtcDate {
    int day = 0;
    int month = 0;
    int year = 0;          // two digits, as transmitted
};

// Latitude/longitude are signed decimal degrees (N and E positive).
// An empty field in the sentence becomes std::nullopt.
struct Gga {
    std::string talker;
    std::optional<UtcTime> time;
    std::optional<double> lat, lon;
    std::optional<int> fix_quality;            // 0..8
    std::optional<int> satellites;
    std::optional<double> hdop;
    std::optional<double> altitude_m;
    std::optional<double> geoid_separation_m;
    std::optional<double> dgps_age_s;
    std::optional<int> dgps_station;
};

struct Rmc {
    std::string talker;
    std::optional<UtcTime> time;
    std::optional<char> status;                // 'A' valid, 'V' warning
    std::optional<double> lat, lon;
    std::optional<double> sog_knots;
    std::optional<double> cog_deg;
    std::optional<UtcDate> date;
    std::optional<double> magnetic_variation_deg;   // E positive, W negative
    std::optional<char> mode;                  // NMEA 2.3+, absent on older talkers
};

struct Hdt {
    std::string talker;
    std::optional<double> heading_true_deg;
};

struct Dpt {
    std::string talker;
    std::optional<double> depth_m;
    std::optional<double> offset_m;
    std::optional<double> max_range_m;
};

struct Mwv {
    std::string talker;
    std::optional<double> angle_deg;
    std::optional<char> reference;             // 'R' relative, 'T' theoretical
    std::optional<double> speed;
    std::optional<char> speed_units;           // K, M, N, S
    std::optional<char> status;                // 'A' or 'V'
};

using Sentence = std::variant<Gga, Rmc, Hdt, Dpt, Mwv>;

std::string_view typeOf(const Sentence& s);      // "GGA", "RMC", ...
const std::string& talkerOf(const Sentence& s);  // "GP", "HE", ...

class NMEAParser {
public:
    // Full pipeline. On success fills `out` and returns nullopt.
    std::optional<ParseError> parse(std::string_view frame, Sentence& out);
    // Same checks, result discarded.
    std::optional<ParseError> parse(std::string_view frame);

    // The individual stages. Each one strips a trailing CR/LF itself, so they can be
    // called on their own. get_fields / field_count_check / format_valid assume the
    // earlier stages passed; the views in `fields` point into `frame`.
    std::optional<ParseError> frame_check(std::string_view frame);
    std::optional<ParseError> checksum_check(std::string_view frame);
    std::optional<ParseError> get_fields(std::string_view frame, std::vector<std::string_view>& out);
    std::optional<ParseError> field_count_check(const std::vector<std::string_view>& fields);
    std::optional<ParseError> format_valid(const std::vector<std::string_view>& fields, Sentence& out);

    // Human-readable reason for the most recent failure ("latitude '9100.000': degrees out of range").
    const std::string& last_detail() const { return detail_; }

private:
    std::optional<ParseError> fail(ParseError e, std::string detail);
    std::string detail_;
};
