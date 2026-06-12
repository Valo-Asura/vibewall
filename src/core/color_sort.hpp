#pragma once

#include "core/model.hpp"

#include <cstdint>
#include <string>

namespace vibewall {

struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

struct Hsv {
  double h = 0.0;
  double s = 0.0;
  double v = 0.0;
};

Hsv rgb_to_hsv(Rgb rgb);
ColorGroup color_group_from_hsv(Hsv hsv);
std::string rgb_to_hex(Rgb rgb);

} // namespace vibewall
