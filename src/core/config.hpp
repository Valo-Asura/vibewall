#pragma once

#include "core/app_paths.hpp"
#include "core/model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vibewall {

struct BackendConfig {
  std::vector<std::string> image;
  std::vector<std::string> image_no_monitor;
  std::vector<std::string> video;
};

struct HooksConfig {
  std::vector<std::string> after_apply;
};

struct AppConfig {
  std::filesystem::path wallpaper_dir;
  std::filesystem::path video_dir;
  std::filesystem::path cache_dir;
  std::filesystem::path db_path;
  DisplayMode display_mode = DisplayMode::Slice;
  bool close_on_selection = true;
  bool enable_directory_watcher = false;
  bool enable_wallhaven = true;
  std::string wallhaven_api_key;
  std::string wallhaven_default_query = "anime landscape";
  double ui_scale = 1.0;
  int target_open_rss_mb = 300;
  int target_daemon_rss_mb = 10;
  BackendConfig backend;
  HooksConfig hooks;
};

AppConfig default_config();
AppConfig load_config(const std::filesystem::path &path = default_config_path());
std::filesystem::path write_default_config_if_missing();

} // namespace vibewall
