#include "core/model.hpp"

#include <stdexcept>

namespace vibewall {

std::string to_string(WallpaperType type) {
  switch (type) {
  case WallpaperType::Image:
    return "image";
  case WallpaperType::Video:
    return "video";
  case WallpaperType::Wallhaven:
    return "wallhaven";
  }
  return "image";
}

WallpaperType wallpaper_type_from_string(const std::string &value) {
  if (value == "image" || value == "static") {
    return WallpaperType::Image;
  }
  if (value == "video") {
    return WallpaperType::Video;
  }
  if (value == "wallhaven") {
    return WallpaperType::Wallhaven;
  }
  throw std::runtime_error("unknown wallpaper type: " + value);
}

std::string to_string(ColorGroup group) {
  switch (group) {
  case ColorGroup::Red:
    return "red";
  case ColorGroup::Orange:
    return "orange";
  case ColorGroup::Yellow:
    return "yellow";
  case ColorGroup::Lime:
    return "lime";
  case ColorGroup::Green:
    return "green";
  case ColorGroup::Cyan:
    return "cyan";
  case ColorGroup::Blue:
    return "blue";
  case ColorGroup::Purple:
    return "purple";
  case ColorGroup::Pink:
    return "pink";
  case ColorGroup::Brown:
    return "brown";
  case ColorGroup::White:
    return "white";
  case ColorGroup::Gray:
    return "gray";
  case ColorGroup::Black:
    return "black";
  }
  return "black";
}

ColorGroup color_group_from_int(int value) {
  if (value < 0 || value > 12) {
    return ColorGroup::Black;
  }
  return static_cast<ColorGroup>(value);
}

std::string to_string(DisplayMode mode) {
  switch (mode) {
  case DisplayMode::Slice:
    return "slice";
  case DisplayMode::Grid:
    return "grid";
  case DisplayMode::Hex:
    return "hex";
  case DisplayMode::Mosaic:
    return "mosaic";
  }
  return "slice";
}

DisplayMode display_mode_from_string(const std::string &value) {
  if (value == "slice" || value == "slices") {
    return DisplayMode::Slice;
  }
  if (value == "grid" || value == "wall") {
    return DisplayMode::Grid;
  }
  if (value == "hex") {
    return DisplayMode::Hex;
  }
  if (value == "mosaic" || value == "masonry") {
    return DisplayMode::Mosaic;
  }
  throw std::runtime_error("unknown display mode: " + value);
}

} // namespace vibewall
