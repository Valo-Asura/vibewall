#include "core/app_paths.hpp"
#include "core/config.hpp"
#include "core/database.hpp"
#include "core/process.hpp"
#include "core/scanner.hpp"
#include "core/wallhaven_client.hpp"
#include "core/wallpaper_apply.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace vibewall;

namespace {
void usage() {
  std::cerr << "usage: vibewall <toggle|picker|scan|restore|random|apply|status|tags|favorite|wallhaven>\n";
}

std::string send_ipc(const std::string &command) {
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error("socket failed");
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const auto socket_path = runtime_socket_path().string();
  if (socket_path.size() >= sizeof(addr.sun_path)) {
    close(fd);
    throw std::runtime_error("socket path too long: " + socket_path);
  }
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path.c_str());
  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    close(fd);
    throw std::runtime_error("vibewall daemon is not running");
  }
  const std::string payload = command + "\n";
  (void)write(fd, payload.c_str(), payload.size());
  char buffer[4096];
  const ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
  close(fd);
  if (n <= 0) {
    return {};
  }
  buffer[n] = '\0';
  return buffer;
}

void ensure_daemon() {
  try {
    (void)send_ipc("status");
    return;
  } catch (...) {
  }

  (void)run_process({"systemctl", "--user", "start", "vibewallrezero-daemon.service"});
  for (int i = 0; i < 150; ++i) {
    usleep(20000);
    try {
      (void)send_ipc("status");
      return;
    } catch (...) {
    }
  }

  const auto result = spawn_detached({"vibewall-daemon"});
  if (result.exit_code != 0) {
    throw std::runtime_error("failed to start daemon: " + result.error);
  }
  for (int i = 0; i < 50; ++i) {
    usleep(20000);
    try {
      (void)send_ipc("status");
      return;
    } catch (...) {
    }
  }
  throw std::runtime_error("daemon did not become ready");
}

WallpaperType type_for_path(const std::string &path) {
  const std::filesystem::path p(path);
  const std::string ext = p.extension().string();
  if (ext == ".mp4" || ext == ".webm" || ext == ".mkv" || ext == ".mov") {
    return WallpaperType::Video;
  }
  return WallpaperType::Image;
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 2) {
      usage();
      return 2;
    }
    const std::string cmd = argv[1];

    if (cmd == "toggle" || cmd == "open" || cmd == "close" || cmd == "status") {
      ensure_daemon();
      std::cout << send_ipc(cmd);
      return 0;
    }

    if (cmd == "picker") {
      std::vector<std::string> args = {"vibewall-picker"};
      for (int i = 2; i < argc; ++i) {
        args.emplace_back(argv[i]);
      }
      const auto result = run_process(args);
      if (result.exit_code != 0) {
        std::cerr << "picker failed: " << result.exit_code << '\n';
      }
      return result.exit_code;
    }

    write_default_config_if_missing();
    AppConfig config = load_config();
    Database db(config.db_path);
    db.migrate();

    if (cmd == "scan") {
      const auto result = scan_wallpapers(db, config);
      std::cout << "images=" << result.images << " videos=" << result.videos
                << " errors=" << result.errors << '\n';
      return result.errors == 0 ? 0 : 1;
    }

    if (cmd == "restore") {
      const auto result = restore_last_wallpaper(db, config);
      std::cout << result.message << '\n';
      return result.ok ? 0 : 1;
    }

    if (cmd == "random") {
      const auto result = apply_random_wallpaper(db, config);
      std::cout << result.message << '\n';
      return result.ok ? 0 : 1;
    }

    if (cmd == "apply") {
      if (argc < 3) {
        throw std::runtime_error("apply requires a path");
      }
      std::string monitor;
      for (int i = 3; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--monitor") {
          monitor = argv[i + 1];
        }
      }
      const std::string path = std::filesystem::absolute(argv[2]).string();
      const auto result = apply_path(db, config, path, type_for_path(path), monitor);
      std::cout << result.message << '\n';
      return result.ok ? 0 : 1;
    }

    if (cmd == "tags") {
      if (argc != 5) {
        throw std::runtime_error("tags usage: vibewall tags <add|remove> <path> <tag>");
      }
      const std::string op = argv[2];
      const std::string path = std::filesystem::absolute(argv[3]).string();
      const std::string tag = argv[4];
      if (op == "add") {
        db.add_tag(path, tag);
      } else if (op == "remove") {
        db.remove_tag(path, tag);
      } else {
        throw std::runtime_error("unknown tags operation: " + op);
      }
      return 0;
    }

    if (cmd == "favorite") {
      if (argc != 4) {
        throw std::runtime_error("favorite usage: vibewall favorite <set|unset> <path>");
      }
      const std::string op = argv[2];
      const std::string path = std::filesystem::absolute(argv[3]).string();
      db.set_favorite(path, op == "set");
      return 0;
    }

    if (cmd == "wallhaven") {
      if (argc < 5 || std::string(argv[2]) != "search") {
        throw std::runtime_error("wallhaven usage: vibewall wallhaven search <query> [--page N]");
      }
      int page = 1;
      for (int i = 4; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--page") {
          page = std::stoi(argv[i + 1]);
        }
      }
      cache_wallhaven_search(db, config, argv[3], page);
      const auto cached = db.cached_wallhaven();
      for (const auto &entry : cached) {
        std::cout << entry.id << ' ' << entry.image_url << '\n';
      }
      return 0;
    }

    usage();
    return 2;
  } catch (const std::exception &err) {
    std::cerr << "vibewall: " << err.what() << '\n';
    return 1;
  }
}
