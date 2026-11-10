#pragma once
#include <string_view>
#include <string>
#include <ctime>


uint8_t checksum(std::string_view body);

std::string utcTime(const ShipState& s);
std::string utcDate(const ShipState& s);

struct ShipState {
    double lat, lon, sog, cog, heading, depth;
    std::tm utc; int centis;
};