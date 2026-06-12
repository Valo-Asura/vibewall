#pragma once

#include "core/config.hpp"
#include "core/database.hpp"

namespace vibewall {

struct ScanResult {
  int images = 0;
  int videos = 0;
  int errors = 0;
};

ScanResult scan_wallpapers(Database &db, const AppConfig &config);

} // namespace vibewall
