#include "core/app_paths.hpp"
#include "core/color_sort.hpp"
#include "core/config.hpp"
#include "core/database.hpp"
#include "core/filters.hpp"
#include "core/process.hpp"

#include <cassert>
#include <filesystem>
#include <iostream>

using namespace vibewall;

namespace {
Wallpaper sample(const std::filesystem::path &path) {
  Wallpaper item;
  item.path = path.string();
  item.name = path.filename().string();
  item.type = WallpaperType::Image;
  item.thumb_path = path.string();
  item.dominant_hex = "#ff0000";
  item.color_group = ColorGroup::Red;
  item.added_at = unix_time_now();
  return item;
}
} // namespace

int main() {
  const auto dir = std::filesystem::temp_directory_path() / ("vibewall-test-" + fnv1a_hex("core"));
  std::filesystem::create_directories(dir);
  const auto db_path = dir / "wallpapers.db";
  Database db(db_path);
  db.migrate();

  auto wp = sample(dir / "alpha red.png");
  db.upsert_wallpaper(wp);
  assert(db.list_wallpapers().size() == 1);
  assert(db.find_by_path(wp.path).has_value());

  db.set_favorite(wp.path, true);
  Filter fav;
  fav.favorites_only = true;
  assert(db.list_wallpapers(fav).size() == 1);
  db.set_favorite(wp.path, false);
  assert(db.list_wallpapers(fav).empty());

  db.add_tag(wp.path, "city");
  Filter tag_filter;
  tag_filter.tags = {"city"};
  assert(db.list_wallpapers(tag_filter).size() == 1);
  db.remove_tag(wp.path, "city");
  assert(db.list_wallpapers(tag_filter).empty());

  Filter query;
  query.query = "red";
  assert(matches_filter(wp, {}, query));
  query.query = "missing";
  assert(!matches_filter(wp, {}, query));

  assert(color_group_from_hsv(rgb_to_hsv({255, 0, 0})) == ColorGroup::Red);
  assert(color_group_from_hsv(rgb_to_hsv({250, 250, 250})) == ColorGroup::White);
  assert(color_group_from_hsv(rgb_to_hsv({30, 30, 30})) == ColorGroup::Black);
  assert(color_group_from_hsv(rgb_to_hsv({80, 62, 42})) == ColorGroup::Brown);

  const auto args =
      substitute_args({"noctalia", "msg", "wallpaper-set", "%monitor%", "%path%"},
                      "/tmp/a path/$(bad).png", "image", "bad", "");
  assert(args.size() == 4);
  assert(args.back() == "/tmp/a path/$(bad).png");

  std::cout << "core tests ok\n";
  return 0;
}
