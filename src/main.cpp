

struct Config {
    double rate_hz = 1.0;            // ticks per second (each tick emits all sentences)
    std::uint64_t seed = 0;
    bool seed_given = false;
    std::string route_path;          // empty = built-in route
    TransportKind transport = TransportKind::UdpUnicast;
    std::string host = "127.0.0.1";
    std::uint16_t port = 10110;
    double speed_kn = 10.0;
    double turn_rate_dps = 3.0;      // max turn rate, degrees per second
    double duration_s = 0.0;         // sim seconds to run, 0 = until Ctrl+C
    std::string log_path;            // optional: log of every message leaving the injector
    FaultConfig faults;              // all probabilities default to 0 (clean feed)
};

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " [options]\n"
        << "\n"
        << "General:\n"
        << "  --rate HZ            ticks per second (default 1)\n"
        << "  --seed N             RNG seed (default: random, printed at startup)\n"
        << "  --route FILE         waypoint file, one \"lat lon\" per line (default: built-in)\n"
        << "  --speed KN           ship speed in knots (default 10)\n"
        << "  --turn-rate DPS      max turn rate in degrees/second (default 3)\n"
        << "  --duration S         stop after S simulated seconds (default: run until Ctrl+C)\n"
        << "  --log FILE           write each message leaving the fault stage to FILE\n"
        << "\n"
        << "Transport:\n"
        << "  --transport MODE     udp | multicast | tcp (default udp)\n"
        << "  --host HOST          destination (default 127.0.0.1, or 239.192.0.1 for multicast)\n"
        << "  --port PORT          destination port (default 10110)\n"
        << "\n"
        << "Faults (probabilities are 0..1 per sentence, default 0):\n"
        << "  --p-checksum P       corrupt the checksum\n"
        << "  --p-truncate P       cut the sentence short\n"
        << "  --p-dup P            send the sentence twice\n"
        << "  --p-reorder P        hold the sentence so it arrives out of order\n"
        << "  --p-burst P          start a burst (hold messages, release together)\n"
        << "  --p-outage P         start a total feed loss\n"
        << "  --outage-s S         outage length in simulated seconds\n"
        << "  --delay-ms MS        fixed delay added to every sentence\n"
        << "  --jitter-ms MS       random extra delay, uniform in [0, MS]\n"
        << "\n"
        << "  -h, --help           show this help\n";
}

void main(int argc, char** argv) {


    return 0;
}