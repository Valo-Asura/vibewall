#pragma once

#include "core/config.hpp"
#include "core/database.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vibewall {

std::vector<WallhavenEntry> parse_wallhaven_response(const std::string &json);
std::vector<WallhavenEntry> wallhaven_search(const AppConfig &config, const std::string &query,
                                             int page);
std::filesystem::path wallhaven_download_preview(const AppConfig &config,
                                                 const WallhavenEntry &entry);
std::filesystem::path wallhaven_download(const AppConfig &config, const WallhavenEntry &entry);
void cache_wallhaven_search(Database &db, const AppConfig &config, const std::string &query,
                            int page);

} // namespace vibewall
