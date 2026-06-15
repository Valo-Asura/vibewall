#include "core/wallpaper_apply.hpp"

#include "core/app_paths.hpp"
#include "core/process.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace vibewall {

namespace {
ApplyResult run_checked(const std::vector<std::string> &argv, const std::string &label) {
  const ProcessResult result = run_process(argv);
  if (result.exit_code != 0) {
    return {false, label + " failed with exit code " + std::to_string(result.exit_code) +
                       (result.error.empty() ? "" : ": " + result.error)};
  }
  return {true, label + " ok"};
}

void remember_video_state(const std::string &path) {
  const auto state = xdg_state_home() / "asura/video-wallpaper";
  ensure_parent_dir(state);
  std::ofstream out(state);
  out << path << '\n';
}

void stop_video_wallpaper() {
  run_process({"pkill", "-x", "mpvpaper"});
  run_process({"pkill", "-f", "mpvpaper --fork --auto-stop"});
  run_process({"pkill", "-f", "/mpvpaper .*--layer background"});
  const auto state = xdg_state_home() / "asura/video-wallpaper";
  std::error_code ec;
  std::filesystem::remove(state, ec);
}

void stop_static_wallpaper_renderer() {
  if (run_process({"systemctl", "--user", "is-active", "--quiet", "hyprpaper.service"}).exit_code == 0) {
    run_process({"systemctl", "--user", "stop", "hyprpaper.service"});
  }
  run_process({"pkill", "-x", "hyprpaper"});
  run_process({"pkill", "-f", "\\.hyprpaper-wrapp"});
}

void run_after_apply_hook(const AppConfig &config, const Wallpaper &wallpaper,
                          const std::string &monitor) {
  if (config.hooks.after_apply.empty()) {
    return;
  }
  const auto args = substitute_args(config.hooks.after_apply, wallpaper.path, to_string(wallpaper.type),
                                    wallpaper.name, monitor);
  if (!args.empty()) {
    spawn_detached(args);
  }
}

void persist_last(Database &db, const Wallpaper &wallpaper, const std::string &monitor) {
  db.set_setting("last.path", wallpaper.path);
  db.set_setting("last.type", to_string(wallpaper.type));
  db.set_setting("last.monitor", monitor);
  db.set_setting("last.name", wallpaper.name);
  db.set_setting("last.used_at", std::to_string(unix_time_now()));
}
} // namespace

ApplyResult apply_wallpaper(Database &db, const AppConfig &config, const Wallpaper &wallpaper,
                            const std::string &monitor) {
  if (!std::filesystem::exists(wallpaper.path) && wallpaper.type != WallpaperType::Wallhaven) {
    return {false, "wallpaper path does not exist: " + wallpaper.path};
  }

  if (wallpaper.type == WallpaperType::Video) {
    stop_video_wallpaper();
    stop_static_wallpaper_renderer();
    const auto args = substitute_args(config.backend.video, wallpaper.path, to_string(wallpaper.type),
                                      wallpaper.name, monitor);
    auto result = run_checked(args, "video backend");
    if (!result.ok) {
      return result;
    }
    remember_video_state(wallpaper.path);
  } else {
    stop_video_wallpaper();
    const auto &template_args = monitor.empty() ? config.backend.image_no_monitor : config.backend.image;
    const auto args = substitute_args(template_args, wallpaper.path, to_string(wallpaper.type),
                                      wallpaper.name, monitor);
    auto result = run_checked(args, "image backend");
    if (!result.ok) {
      return result;
    }
  }

  persist_last(db, wallpaper, monitor);
  db.set_setting("last.applied_path", wallpaper.path);
  if (wallpaper.type != WallpaperType::Video) {
    run_after_apply_hook(config, wallpaper, monitor);
  }
  return {true, "applied " + wallpaper.path};
}

ApplyResult apply_path(Database &db, const AppConfig &config, const std::string &path,
                       WallpaperType type, const std::string &monitor) {
  Wallpaper wallpaper;
  if (const auto existing = db.find_by_path(path); existing.has_value()) {
    wallpaper = *existing;
  } else {
    wallpaper.path = path;
    wallpaper.name = std::filesystem::path(path).filename().string();
    wallpaper.type = type;
    wallpaper.thumb_path = path;
    wallpaper.added_at = unix_time_now();
  }
  return apply_wallpaper(db, config, wallpaper, monitor);
}

ApplyResult restore_last_wallpaper(Database &db, const AppConfig &config) {
  const auto path = db.setting("last.path");
  if (!path.has_value() || path->empty()) {
    return {true, "nothing to restore"};
  }
  const WallpaperType type =
      wallpaper_type_from_string(db.setting("last.type").value_or("image"));
  const std::string monitor = db.setting("last.monitor").value_or("");
  return apply_path(db, config, *path, type, monitor);
}

ApplyResult apply_random_wallpaper(Database &db, const AppConfig &config) {
  const auto wallpaper = db.random_wallpaper();
  if (!wallpaper.has_value()) {
    return {false, "no indexed wallpapers; run vibewall scan first"};
  }
  return apply_wallpaper(db, config, *wallpaper);
}

} // namespace vibewall
