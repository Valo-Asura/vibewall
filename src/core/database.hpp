#pragma once

#include "core/model.hpp"

#include <filesystem>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace vibewall {

class Statement {
public:
  Statement(sqlite3 *db, const std::string &sql);
  ~Statement();
  Statement(const Statement &) = delete;
  Statement &operator=(const Statement &) = delete;
  Statement(Statement &&other) noexcept;
  Statement &operator=(Statement &&other) noexcept;

  sqlite3_stmt *get() const { return stmt_; }
  bool step();
  void reset();

private:
  sqlite3_stmt *stmt_ = nullptr;
};

class Database {
public:
  explicit Database(const std::filesystem::path &path);
  ~Database();
  Database(const Database &) = delete;
  Database &operator=(const Database &) = delete;

  void migrate();
  void upsert_wallpaper(const Wallpaper &wallpaper);
  std::vector<Wallpaper> list_wallpapers(const Filter &filter = {}) const;
  std::optional<Wallpaper> find_by_path(const std::string &path) const;
  std::optional<Wallpaper> random_wallpaper() const;
  void set_favorite(const std::string &path, bool favorite);
  void add_tag(const std::string &path, const std::string &tag);
  void remove_tag(const std::string &path, const std::string &tag);
  std::vector<std::string> tags_for_wallpaper(int wallpaper_id) const;
  void set_setting(const std::string &key, const std::string &value);
  std::optional<std::string> setting(const std::string &key) const;
  void cache_wallhaven(const WallhavenEntry &entry);
  std::vector<WallhavenEntry> cached_wallhaven() const;
  sqlite3 *raw() const { return db_; }

private:
  sqlite3 *db_ = nullptr;

  void exec(const std::string &sql) const;
  std::optional<int> wallpaper_id_for_path(const std::string &path) const;
};

} // namespace vibewall
