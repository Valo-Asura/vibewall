#pragma once

#include "core/config.hpp"
#include "core/database.hpp"
#include "core/model.hpp"

#include <optional>
#include <string>

namespace vibewall {

struct ApplyResult {
  bool ok = false;
  std::string message;
};

ApplyResult apply_wallpaper(Database &db, const AppConfig &config, const Wallpaper &wallpaper,
                            const std::string &monitor = {});
ApplyResult apply_path(Database &db, const AppConfig &config, const std::string &path,
                       WallpaperType type, const std::string &monitor = {});
ApplyResult restore_last_wallpaper(Database &db, const AppConfig &config);
ApplyResult apply_random_wallpaper(Database &db, const AppConfig &config);

} // namespace vibewall
