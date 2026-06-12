#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vibewall {

enum class WallpaperType {
  Image,
  Video,
  Wallhaven,
};

enum class ColorGroup {
  Red = 0,
  Orange = 1,
  Yellow = 2,
  Lime = 3,
  Green = 4,
  Cyan = 5,
  Blue = 6,
  Purple = 7,
  Pink = 8,
  Brown = 9,
  White = 10,
  Gray = 11,
  Black = 12,
};

enum class DisplayMode {
  Slice,
  Grid,
  Hex,
  Mosaic,
};

struct Wallpaper {
  int id = 0;
  std::string path;
  std::string name;
  WallpaperType type = WallpaperType::Image;
  std::string thumb_path;
  std::string dominant_hex = "#000000";
  double hue = 0.0;
  double saturation = 0.0;
  double value = 0.0;
  ColorGroup color_group = ColorGroup::Black;
  bool favorite = false;
  int width = 0;
  int height = 0;
  std::int64_t added_at = 0;
  std::optional<std::int64_t> last_used_at;
};

struct Filter {
  std::string query;
  std::optional<WallpaperType> type;
  std::optional<ColorGroup> color_group;
  bool favorites_only = false;
  bool recent_first = false;
  std::vector<std::string> tags;
};

struct WallhavenEntry {
  std::string id;
  std::string page_url;
  std::string image_url;
  std::string preview_url;
  std::string thumb_path;
  int width = 0;
  int height = 0;
  std::string purity;
  std::string category;
  std::string colors_json;
  std::int64_t cached_at = 0;
};

std::string to_string(WallpaperType type);
WallpaperType wallpaper_type_from_string(const std::string &value);
std::string to_string(ColorGroup group);
ColorGroup color_group_from_int(int value);
std::string to_string(DisplayMode mode);
DisplayMode display_mode_from_string(const std::string &value);

} // namespace vibewall
