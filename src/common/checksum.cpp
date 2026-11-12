#include "common/checksum.hpp"
#include <cstdint>
#include <cctype>

uint8_t checksum(std::string_view body) {
    uint8_t cs = 0;
    for (char c : body) {
        cs ^= static_cast<uint8_t>(c);
    }
    return cs;
}

bool is_hex_char(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}