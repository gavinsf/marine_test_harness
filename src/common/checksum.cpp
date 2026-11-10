#include "common/checksum.hpp"
#include <cstdint>

uint8_t checksum(std::string_view body) {
    uint8_t cs = 0;
    for (char c : body) {
        cs ^= static_cast<uint8_t>(c);
    }
    return cs;
}
