// nmea_sim: simulates a vessel sailing a waypoint loop and streams its NMEA 0183
// sentences (GGA, RMC, HDT, DPT, MWV) over UDP.
//
// Each tick (--rate per second) takes one snapshot of the ship state and sends all
// five sentences built from it, so every sentence in a tick shares one timestamp.
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "common/ship_state.hpp"
#include "sim/builder.hpp"
#include "udp/sender.hpp"

namespace {

std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

// ---------------------------------------------------------------- navigation

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;

struct Pos {
    double lat, lon;   // decimal degrees
};

double rad(double d) { return d * kPi / 180.0; }
double deg(double r) { return r * 180.0 / kPi; }

double distanceNm(Pos a, Pos b) {
    const double dLat = rad(b.lat - a.lat), dLon = rad(b.lon - a.lon);
    const double h = std::sin(dLat / 2) * std::sin(dLat / 2) +
                     std::cos(rad(a.lat)) * std::cos(rad(b.lat)) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * kEarthRadiusNm * std::asin(std::min(1.0, std::sqrt(h)));
}

double bearingDeg(Pos a, Pos b) {
    const double dLon = rad(b.lon - a.lon);
    const double y = std::sin(dLon) * std::cos(rad(b.lat));
    const double x = std::cos(rad(a.lat)) * std::sin(rad(b.lat)) -
                     std::sin(rad(a.lat)) * std::cos(rad(b.lat)) * std::cos(dLon);
    return std::fmod(deg(std::atan2(y, x)) + 360.0, 360.0);
}

Pos destination(Pos from, double bearing, double nm) {
    const double d = nm / kEarthRadiusNm, b = rad(bearing);
    const double lat1 = rad(from.lat), lon1 = rad(from.lon);
    const double lat2 = std::asin(std::sin(lat1) * std::cos(d) + std::cos(lat1) * std::sin(d) * std::cos(b));
    const double lon2 = lon1 + std::atan2(std::sin(b) * std::sin(d) * std::cos(lat1),
                                          std::cos(d) - std::sin(lat1) * std::sin(lat2));
    return {deg(lat2), deg(lon2)};
}

// A loop in Puget Sound, a few nautical miles per leg.
const std::vector<Pos> kWaypoints = {
    {47.6000, -122.4000}, {47.6500, -122.4300}, {47.7000, -122.3800}, {47.6500, -122.3500}};

struct Vessel {
    Pos pos = kWaypoints[0];
    std::size_t target = 1;        // waypoint being sailed to
    double speedKnots = 10.0;
    double courseDeg = bearingDeg(kWaypoints[0], kWaypoints[1]);

    // Sail for `seconds`, moving on to the next waypoint (and looping back to the
    // first after the last) whenever the current one is reached.
    void advance(double seconds) {
        double remaining = speedKnots * seconds / 3600.0;   // nautical miles to cover
        for (int guard = 0; remaining > 0.0 && guard < 100; ++guard) {
            const Pos goal = kWaypoints[target];
            const double d = distanceNm(pos, goal);
            if (d <= remaining) {
                pos = goal;
                remaining -= d;
                target = (target + 1) % kWaypoints.size();
            } else {
                pos = destination(pos, bearingDeg(pos, goal), remaining);
                remaining = 0.0;
            }
        }
        courseDeg = bearingDeg(pos, kWaypoints[target]);
    }
};

// ---------------------------------------------------------------- command line

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 10110;     // conventional NMEA-over-UDP port
    int rate = 10;                  // ticks per second (1-50)
    long long count = 0;            // ticks to run; 0 = until Ctrl-C
    double speed = 10.0;            // knots
};

void usage() {
    std::cerr << "usage: nmea_sim [--host ADDR] [--port N] [--rate 1-50] [--count N] [--speed KNOTS]\n"
                 "  --host   destination IPv4 address            (127.0.0.1)\n"
                 "  --port   destination UDP port                (10110)\n"
                 "  --rate   ticks per second; each tick sends GGA, RMC, HDT, DPT and MWV  (10)\n"
                 "  --count  ticks to send, 0 = run until Ctrl-C (0)\n"
                 "  --speed  vessel speed in knots               (10)\n";
}

template <class T>
bool parseNumber(const std::string& s, T& out) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return !s.empty() && ec == std::errc{} && p == s.data() + s.size();
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--help" || flag == "-h") return false;
        if (i + 1 >= argc) {
            std::cerr << "nmea_sim: missing value for " << flag << "\n";
            return false;
        }
        const std::string value = argv[++i];
        bool ok = true;
        if (flag == "--host") o.host = value;
        else if (flag == "--port") { int p = 0; ok = parseNumber(value, p) && p >= 1 && p <= 65535; o.port = static_cast<std::uint16_t>(p); }
        else if (flag == "--rate") ok = parseNumber(value, o.rate) && o.rate >= 1 && o.rate <= 50;
        else if (flag == "--count") ok = parseNumber(value, o.count) && o.count >= 0;
        else if (flag == "--speed") ok = parseNumber(value, o.speed) && o.speed >= 0.0 && o.speed <= 100.0;
        else {
            std::cerr << "nmea_sim: unknown option " << flag << "\n";
            return false;
        }
        if (!ok) {
            std::cerr << "nmea_sim: invalid value '" << value << "' for " << flag << "\n";
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        usage();
        return 2;
    }

    try {
        UdpSender send(opt.host, opt.port);
        Vessel ship;
        ship.speedKnots = opt.speed;

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        using Clock = std::chrono::steady_clock;
        const auto period = std::chrono::nanoseconds(1'000'000'000LL / opt.rate);
        const auto start = Clock::now();
        auto next = start;
        long long ticks = 0, sentences = 0;

        while (!g_stop && (opt.count == 0 || ticks < opt.count)) {
            const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            if (ticks > 0) ship.advance(1.0 / opt.rate);   // simulated time, so speed is exact at any rate

            ShipState s{};
            s.lat = ship.pos.lat;
            s.lon = ship.pos.lon;
            s.sog = ship.speedKnots;
            s.cog = ship.courseDeg;
            s.heading = ship.courseDeg;
            s.depth = 25.0 + 10.0 * std::sin(2.0 * kPi * elapsed / 120.0);
            s.wind_angle = 180.0 + 90.0 * std::sin(2.0 * kPi * elapsed / 90.0);
            s.wind_speed = 15.0 + 5.0 * std::sin(2.0 * kPi * elapsed / 40.0);

            // Wall-clock UTC for the sentence timestamps.
            const auto now = std::chrono::system_clock::now();
            const std::time_t secs = std::chrono::system_clock::to_time_t(now);
            gmtime_r(&secs, &s.utc);
            s.centis = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000 / 10);

            for (const std::string& sentence : {gga(s), rmc(s), hdt(s), dpt(s), mwv(s)}) {
                send(sentence);
                ++sentences;
            }
            ++ticks;

            // Fixed schedule rather than sleep(period), so timing error does not accumulate.
            next += period;
            std::this_thread::sleep_until(next);
        }

        std::cerr << "nmea_sim: sent " << sentences << " sentences in " << ticks << " ticks to "
                  << opt.host << ":" << opt.port << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "nmea_sim: " << e.what() << "\n";
        return 1;
    }
}
