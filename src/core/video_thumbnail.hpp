#pragma once

#include "core/config.hpp"
#include "core/thumbnail_cache.hpp"

#include <filesystem>

namespace vibewall {

ThumbnailInfo create_video_thumbnail(const AppConfig &config, const std::filesystem::path &source);
bool is_video_extension(const std::filesystem::path &path);
bool is_image_extension(const std::filesystem::path &path);

} // namespace vibewall
