#pragma once

#include "core/model.hpp"

#include <string>
#include <vector>

namespace vibewall {

bool matches_filter(const Wallpaper &wallpaper, const std::vector<std::string> &tags,
                    const Filter &filter);
std::string lowercase(std::string value);

} // namespace vibewall
