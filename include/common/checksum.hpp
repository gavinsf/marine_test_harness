#pragma once
#include <cstdint>
#include <string_view>

uint8_t checksum(std::string_view body);
bool is_hex_char(char c);
int hex_digit(char c);   // 0-15, or -1 if not a hex digit