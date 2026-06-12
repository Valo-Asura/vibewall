#include "core/color_sort.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace vibewall {

Hsv rgb_to_hsv(Rgb rgb) {
  const double r = static_cast<double>(rgb.r) / 255.0;
  const double g = static_cast<double>(rgb.g) / 255.0;
  const double b = static_cast<double>(rgb.b) / 255.0;
  const double max_value = std::max({r, g, b});
  const double min_value = std::min({r, g, b});
  const double delta = max_value - min_value;

  double hue = 0.0;
  if (delta > 0.00001) {
    if (max_value == r) {
      hue = 60.0 * std::fmod(((g - b) / delta), 6.0);
    } else if (max_value == g) {
      hue = 60.0 * (((b - r) / delta) + 2.0);
    } else {
      hue = 60.0 * (((r - g) / delta) + 4.0);
    }
  }
  if (hue < 0.0) {
    hue += 360.0;
  }
  const double saturation = max_value <= 0.0 ? 0.0 : delta / max_value;
  return {hue, saturation, max_value};
}

ColorGroup color_group_from_hsv(Hsv hsv) {
  if (hsv.v < 0.18) {
    return ColorGroup::Black;
  }
  if (hsv.s < 0.16) {
    if (hsv.v > 0.78) {
      return ColorGroup::White;
    }
    return ColorGroup::Gray;
  }
  if (hsv.h >= 20.0 && hsv.h < 50.0 && hsv.s < 0.55 && hsv.v < 0.62) {
    return ColorGroup::Brown;
  }
  if (hsv.h < 15.0 || hsv.h >= 345.0) {
    return ColorGroup::Red;
  }
  if (hsv.h < 42.0) {
    return ColorGroup::Orange;
  }
  if (hsv.h < 68.0) {
    return ColorGroup::Yellow;
  }
  if (hsv.h < 92.0) {
    return ColorGroup::Lime;
  }
  if (hsv.h < 155.0) {
    return ColorGroup::Green;
  }
  if (hsv.h < 195.0) {
    return ColorGroup::Cyan;
  }
  if (hsv.h < 250.0) {
    return ColorGroup::Blue;
  }
  if (hsv.h < 292.0) {
    return ColorGroup::Purple;
  }
  if (hsv.h < 345.0) {
    return ColorGroup::Pink;
  }
  return ColorGroup::Red;
}

std::string rgb_to_hex(Rgb rgb) {
  std::ostringstream out;
  out << '#' << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(rgb.r)
      << std::setw(2) << static_cast<int>(rgb.g) << std::setw(2) << static_cast<int>(rgb.b);
  return out.str();
}

} // namespace vibewall
