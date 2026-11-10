#pragma once
#include <ctime>

struct ShipState {
    double lat, lon, sog, cog, heading, depth;
    std::tm utc; int centis;
};