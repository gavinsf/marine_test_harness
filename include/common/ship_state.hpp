#pragma once
#include <ctime>

struct ShipState {
    double lat, lon, sog, cog, heading, depth;
    std::tm utc; int centis;
    double wind_angle = 0.0;   // degrees, relative to the bow
    double wind_speed = 0.0;   // knots
};
