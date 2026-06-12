#include "core/filters.hpp"

#include <algorithm>
#include <cctype>

namespace vibewall {

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool matches_filter(const Wallpaper &wallpaper, const std::vector<std::string> &tags,
                    const Filter &filter) {
  if (!filter.query.empty()) {
    const std::string query = lowercase(filter.query);
    const std::string name = lowercase(wallpaper.name);
    const std::string path = lowercase(wallpaper.path);
    if (name.find(query) == std::string::npos && path.find(query) == std::string::npos) {
      return false;
    }
  }
  if (filter.type.has_value() && wallpaper.type != *filter.type) {
    return false;
  }
  if (filter.color_group.has_value() && wallpaper.color_group != *filter.color_group) {
    return false;
  }
  if (filter.favorites_only && !wallpaper.favorite) {
    return false;
  }
  for (const std::string &wanted : filter.tags) {
    const auto found = std::find(tags.begin(), tags.end(), wanted);
    if (found == tags.end()) {
      return false;
    }
  }
  return true;
}

} // namespace vibewall
