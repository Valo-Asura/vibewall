#pragma once

#include "core/color_sort.hpp"
#include "core/config.hpp"

#include <filesystem>

namespace vibewall {

struct ThumbnailInfo {
  std::filesystem::path path;
  Rgb average;
  int width = 0;
  int height = 0;
};

ThumbnailInfo create_image_thumbnail(const AppConfig &config, const std::filesystem::path &source);
ThumbnailInfo analyze_thumbnail(const std::filesystem::path &thumb_path);

} // namespace vibewall
