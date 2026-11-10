#pragma once
#include <string>
#include <cstdio>
#include "ship_state.hpp"
#include "initializer_list"

std::string gga(const ShipState& s);
std::string rmc(const ShipState& s);
std::string hdt(const ShipState& s);
std::string dpt(const ShipState& s);
std::string frame(const std::string& body);