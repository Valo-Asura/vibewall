#include "core/config.hpp"

#include "core/app_paths.hpp"

#include <fstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace vibewall {

namespace {
std::filesystem::path default_wallpaper_dir() {
  const auto short_dir = home_dir() / "Wallpaper";
  if (std::filesystem::exists(short_dir)) {
    return short_dir;
  }
  return home_dir() / "Pictures/Wallpapers";
}

std::vector<std::string> read_string_array(const toml::table &table, std::string_view key,
                                           std::vector<std::string> fallback) {
  const toml::array *array = table[key].as_array();
  if (array == nullptr) {
    return fallback;
  }
  std::vector<std::string> out;
  array->for_each([&](auto &&node) {
    if constexpr (toml::is_string<decltype(node)>) {
      out.push_back(*node);
    }
  });
  return out.empty() ? fallback : out;
}

std::filesystem::path read_path(const toml::table &table, std::string_view key,
                                const std::filesystem::path &fallback) {
  if (auto value = table[key].value<std::string>()) {
    return expand_user_path(*value);
  }
  return fallback;
}
} // namespace

AppConfig default_config() {
  AppConfig cfg;
  cfg.wallpaper_dir = default_wallpaper_dir();
  cfg.video_dir = default_wallpaper_dir();
  cfg.cache_dir = xdg_cache_home() / "vibewallREzero";
  cfg.db_path = xdg_data_home() / "vibewallREzero/wallpapers.db";
  cfg.backend.image = {"noctalia", "msg", "wallpaper-set", "%monitor%", "%path%"};
  cfg.backend.image_no_monitor = {"noctalia", "msg", "wallpaper-set", "%path%"};
  cfg.backend.video = {"mpvpaper", "--fork", "--auto-pause", "--layer", "background",
                       "--mpv-options", "no-audio loop hwdec=auto-safe profile=fast",
                       "%monitor_or_star%", "%path%"};
  cfg.hooks.after_apply = {"matugen", "image", "--source-color-index", "0", "%path%"};
  return cfg;
}

AppConfig load_config(const std::filesystem::path &path) {
  AppConfig cfg = default_config();
  if (!std::filesystem::exists(path)) {
    return cfg;
  }

  toml::table table;
  try {
    table = toml::parse_file(path.string());
  } catch (const toml::parse_error &err) {
    throw std::runtime_error("failed to parse config " + path.string() + ": " +
                             std::string(err.description()));
  }

  cfg.wallpaper_dir = read_path(table, "wallpaper_dir", cfg.wallpaper_dir);
  cfg.video_dir = read_path(table, "video_dir", cfg.video_dir);
  const auto legacy_dir = home_dir() / "Pictures/Wallpapers";
  const auto live_dir = home_dir() / "Wallpaper";
  if (cfg.wallpaper_dir == legacy_dir && !std::filesystem::exists(legacy_dir) &&
      std::filesystem::exists(live_dir)) {
    cfg.wallpaper_dir = live_dir;
  }
  if (cfg.video_dir == legacy_dir && !std::filesystem::exists(legacy_dir) &&
      std::filesystem::exists(live_dir)) {
    cfg.video_dir = live_dir;
  }
  cfg.cache_dir = read_path(table, "cache_dir", cfg.cache_dir);
  cfg.db_path = read_path(table, "db_path", cfg.db_path);
  if (auto value = table["display_mode"].value<std::string>()) {
    cfg.display_mode = display_mode_from_string(*value);
  }
  cfg.close_on_selection = table["close_on_selection"].value_or(cfg.close_on_selection);
  cfg.enable_directory_watcher = table["enable_directory_watcher"].value_or(cfg.enable_directory_watcher);
  cfg.enable_wallhaven = table["enable_wallhaven"].value_or(cfg.enable_wallhaven);
  cfg.wallhaven_api_key = table["wallhaven_api_key"].value_or(cfg.wallhaven_api_key);
  cfg.wallhaven_default_query =
      table["wallhaven_default_query"].value_or(cfg.wallhaven_default_query);
  cfg.ui_scale = table["ui_scale"].value_or(cfg.ui_scale);
  cfg.target_open_rss_mb = table["target_open_rss_mb"].value_or(cfg.target_open_rss_mb);
  cfg.target_daemon_rss_mb = table["target_daemon_rss_mb"].value_or(cfg.target_daemon_rss_mb);

  if (const toml::table *backend = table["backend"].as_table()) {
    cfg.backend.image = read_string_array(*backend, "image", cfg.backend.image);
    cfg.backend.image_no_monitor =
        read_string_array(*backend, "image_no_monitor", cfg.backend.image_no_monitor);
    cfg.backend.video = read_string_array(*backend, "video", cfg.backend.video);
  }
  if (const toml::table *hooks = table["hooks"].as_table()) {
    cfg.hooks.after_apply = read_string_array(*hooks, "after_apply", cfg.hooks.after_apply);
  }
  return cfg;
}

std::filesystem::path write_default_config_if_missing() {
  const auto path = default_config_path();
  if (std::filesystem::exists(path)) {
    return path;
  }
  ensure_parent_dir(path);
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("failed to write " + path.string());
  }
  out << "wallpaper_dir = \"~/Wallpaper\"\n"
      << "video_dir = \"~/Wallpaper\"\n"
      << "cache_dir = \"~/.cache/vibewallREzero\"\n"
      << "db_path = \"~/.local/share/vibewallREzero/wallpapers.db\"\n"
      << "display_mode = \"slice\"\n"
      << "close_on_selection = true\n"
      << "enable_directory_watcher = false\n"
      << "enable_wallhaven = true\n"
      << "wallhaven_api_key = \"\"\n"
      << "wallhaven_default_query = \"anime landscape\"\n\n"
      << "[backend]\n"
      << "image = [\"noctalia\", \"msg\", \"wallpaper-set\", \"%monitor%\", \"%path%\"]\n"
      << "image_no_monitor = [\"noctalia\", \"msg\", \"wallpaper-set\", \"%path%\"]\n"
      << "video = [\"mpvpaper\", \"--fork\", \"--auto-pause\", \"--layer\", \"background\", "
         "\"--mpv-options\", \"no-audio loop hwdec=auto-safe profile=fast\", "
         "\"%monitor_or_star%\", \"%path%\"]\n\n"
      << "[hooks]\n"
      << "after_apply = [\"matugen\", \"image\", \"--source-color-index\", \"0\", \"%path%\"]\n";
  return path;
}

} // namespace vibewall
