#pragma once
#include <string>
#include <cstdio>
#include "common/ship_state.hpp"

std::string fmtLat(double lat);
std::string fmtLon(double lon);
std::string fmtFixed(double v, int decimals);

std::string gga(const ShipState& s);
std::string rmc(const ShipState& s);
std::string hdt(const ShipState& s);
std::string dpt(const ShipState& s);
std::string frame(const std::string& body);