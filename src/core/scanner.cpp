#include "core/scanner.hpp"

#include "core/app_paths.hpp"
#include "core/color_sort.hpp"
#include "core/thumbnail_cache.hpp"
#include "core/video_thumbnail.hpp"

#include <filesystem>
#include <iostream>

namespace vibewall {

namespace {
void index_one(Database &db, const AppConfig &config, const std::filesystem::path &path,
               WallpaperType type, ScanResult &result) {
  ThumbnailInfo thumb = type == WallpaperType::Video ? create_video_thumbnail(config, path)
                                                     : create_image_thumbnail(config, path);
  const Hsv hsv = rgb_to_hsv(thumb.average);
  Wallpaper item;
  item.path = std::filesystem::absolute(path).string();
  item.name = path.filename().string();
  item.type = type;
  item.thumb_path = thumb.path.string();
  item.dominant_hex = rgb_to_hex(thumb.average);
  item.hue = hsv.h;
  item.saturation = hsv.s;
  item.value = hsv.v;
  item.color_group = color_group_from_hsv(hsv);
  item.width = thumb.width;
  item.height = thumb.height;
  item.added_at = unix_time_now();
  db.upsert_wallpaper(item);
  if (type == WallpaperType::Video) {
    ++result.videos;
  } else {
    ++result.images;
  }
}

void scan_dir(Database &db, const AppConfig &config, const std::filesystem::path &dir,
              ScanResult &result) {
  if (!std::filesystem::exists(dir)) {
    return;
  }
  for (const auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto path = entry.path();
    try {
      if (is_image_extension(path)) {
        index_one(db, config, path, WallpaperType::Image, result);
      } else if (is_video_extension(path)) {
        index_one(db, config, path, WallpaperType::Video, result);
      }
    } catch (const std::exception &err) {
      ++result.errors;
      std::cerr << "scan warning: " << path << ": " << err.what() << '\n';
    }
  }
}
} // namespace

ScanResult scan_wallpapers(Database &db, const AppConfig &config) {
  ensure_dir(config.cache_dir / "thumbs");
  ScanResult result;
  scan_dir(db, config, config.wallpaper_dir, result);
  if (config.video_dir != config.wallpaper_dir) {
    scan_dir(db, config, config.video_dir, result);
  }
  return result;
}

} // namespace vibewall
