#include "core/app_paths.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

namespace vibewall {

namespace {
std::filesystem::path env_path(const char *name, const std::filesystem::path &fallback) {
  if (const char *value = std::getenv(name); value != nullptr && value[0] != '\0') {
    return std::filesystem::path(value);
  }
  return fallback;
}
} // namespace

std::filesystem::path home_dir() {
  if (const char *value = std::getenv("HOME"); value != nullptr && value[0] != '\0') {
    return std::filesystem::path(value);
  }
  throw std::runtime_error("HOME is not set");
}

std::filesystem::path expand_user_path(const std::string &path) {
  if (path == "~") {
    return home_dir();
  }
  if (path.starts_with("~/")) {
    return home_dir() / path.substr(2);
  }
  return std::filesystem::path(path);
}

std::filesystem::path xdg_config_home() {
  return env_path("XDG_CONFIG_HOME", home_dir() / ".config");
}

std::filesystem::path xdg_cache_home() {
  return env_path("XDG_CACHE_HOME", home_dir() / ".cache");
}

std::filesystem::path xdg_data_home() {
  return env_path("XDG_DATA_HOME", home_dir() / ".local/share");
}

std::filesystem::path xdg_state_home() {
  return env_path("XDG_STATE_HOME", home_dir() / ".local/state");
}

std::filesystem::path xdg_runtime_dir() {
  return env_path("XDG_RUNTIME_DIR", std::filesystem::path("/tmp"));
}

std::filesystem::path default_config_path() {
  return xdg_config_home() / "vibewallREzero/config.toml";
}

std::filesystem::path runtime_socket_path() {
  return xdg_runtime_dir() / "vibewallrezero.sock";
}

void ensure_parent_dir(const std::filesystem::path &path) {
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
}

void ensure_dir(const std::filesystem::path &path) {
  std::filesystem::create_directories(path);
}

std::string fnv1a_hex(const std::string &value) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (unsigned char c : value) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << hash;
  return out.str();
}

std::int64_t unix_time_now() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

} // namespace vibewall
