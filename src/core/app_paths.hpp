#pragma once

#include <filesystem>
#include <string>

namespace vibewall {

std::filesystem::path home_dir();
std::filesystem::path expand_user_path(const std::string &path);
std::filesystem::path xdg_config_home();
std::filesystem::path xdg_cache_home();
std::filesystem::path xdg_data_home();
std::filesystem::path xdg_state_home();
std::filesystem::path xdg_runtime_dir();
std::filesystem::path default_config_path();
std::filesystem::path runtime_socket_path();
void ensure_parent_dir(const std::filesystem::path &path);
void ensure_dir(const std::filesystem::path &path);
std::string fnv1a_hex(const std::string &value);
std::int64_t unix_time_now();

} // namespace vibewall
