#include "core/database.hpp"

#include "core/app_paths.hpp"
#include "core/filters.hpp"

#include <stdexcept>

namespace vibewall {

namespace {
std::string column_text(sqlite3_stmt *stmt, int col) {
  const auto *text = sqlite3_column_text(stmt, col);
  return text == nullptr ? "" : reinterpret_cast<const char *>(text);
}

Wallpaper row_to_wallpaper(sqlite3_stmt *stmt) {
  Wallpaper item;
  item.id = sqlite3_column_int(stmt, 0);
  item.path = column_text(stmt, 1);
  item.name = column_text(stmt, 2);
  item.type = wallpaper_type_from_string(column_text(stmt, 3));
  item.thumb_path = column_text(stmt, 4);
  item.dominant_hex = column_text(stmt, 5);
  item.hue = sqlite3_column_double(stmt, 6);
  item.saturation = sqlite3_column_double(stmt, 7);
  item.value = sqlite3_column_double(stmt, 8);
  item.color_group = color_group_from_int(sqlite3_column_int(stmt, 9));
  item.favorite = sqlite3_column_int(stmt, 10) != 0;
  item.width = sqlite3_column_int(stmt, 11);
  item.height = sqlite3_column_int(stmt, 12);
  item.added_at = sqlite3_column_int64(stmt, 13);
  if (sqlite3_column_type(stmt, 14) != SQLITE_NULL) {
    item.last_used_at = sqlite3_column_int64(stmt, 14);
  }
  return item;
}
} // namespace

Statement::Statement(sqlite3 *db, const std::string &sql) {
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
    throw std::runtime_error("sqlite prepare failed: " + sql + ": " + sqlite3_errmsg(db));
  }
}

Statement::~Statement() {
  if (stmt_ != nullptr) {
    sqlite3_finalize(stmt_);
  }
}

Statement::Statement(Statement &&other) noexcept : stmt_(other.stmt_) {
  other.stmt_ = nullptr;
}

Statement &Statement::operator=(Statement &&other) noexcept {
  if (this != &other) {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
    stmt_ = other.stmt_;
    other.stmt_ = nullptr;
  }
  return *this;
}

bool Statement::step() {
  const int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) {
    return true;
  }
  if (rc == SQLITE_DONE) {
    return false;
  }
  throw std::runtime_error("sqlite step failed: " + std::string(sqlite3_errmsg(sqlite3_db_handle(stmt_))));
}

void Statement::reset() {
  sqlite3_reset(stmt_);
  sqlite3_clear_bindings(stmt_);
}

Database::Database(const std::filesystem::path &path) {
  ensure_parent_dir(path);
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    const std::string err = db_ == nullptr ? "unknown" : sqlite3_errmsg(db_);
    throw std::runtime_error("failed to open sqlite db " + path.string() + ": " + err);
  }
  exec("PRAGMA foreign_keys = ON");
}

Database::~Database() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
  }
}

void Database::exec(const std::string &sql) const {
  char *err = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
    std::string message = err == nullptr ? sqlite3_errmsg(db_) : err;
    sqlite3_free(err);
    throw std::runtime_error("sqlite exec failed: " + message + ": " + sql);
  }
}

void Database::migrate() {
  exec("CREATE TABLE IF NOT EXISTS wallpapers ("
       "id INTEGER PRIMARY KEY,"
       "path TEXT UNIQUE NOT NULL,"
       "name TEXT NOT NULL,"
       "type TEXT NOT NULL CHECK(type IN ('image','video','wallhaven')),"
       "thumb_path TEXT NOT NULL,"
       "dominant_hex TEXT,"
       "hue REAL NOT NULL DEFAULT 0,"
       "saturation REAL NOT NULL DEFAULT 0,"
       "value REAL NOT NULL DEFAULT 0,"
       "color_group INTEGER NOT NULL DEFAULT 0,"
       "favorite INTEGER NOT NULL DEFAULT 0,"
       "width INTEGER NOT NULL DEFAULT 0,"
       "height INTEGER NOT NULL DEFAULT 0,"
       "added_at INTEGER NOT NULL,"
       "last_used_at INTEGER)");
  exec("CREATE TABLE IF NOT EXISTS tags (id INTEGER PRIMARY KEY, name TEXT UNIQUE NOT NULL)");
  exec("CREATE TABLE IF NOT EXISTS wallpaper_tags ("
       "wallpaper_id INTEGER NOT NULL,"
       "tag_id INTEGER NOT NULL,"
       "PRIMARY KEY (wallpaper_id, tag_id),"
       "FOREIGN KEY (wallpaper_id) REFERENCES wallpapers(id) ON DELETE CASCADE,"
       "FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE)");
  exec("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
  exec("CREATE TABLE IF NOT EXISTS wallhaven_cache ("
       "id TEXT PRIMARY KEY,"
       "page_url TEXT NOT NULL,"
       "image_url TEXT NOT NULL,"
       "preview_url TEXT NOT NULL,"
       "thumb_path TEXT,"
       "width INTEGER,"
       "height INTEGER,"
       "purity TEXT,"
       "category TEXT,"
       "colors TEXT,"
       "cached_at INTEGER NOT NULL)");
  exec("CREATE INDEX IF NOT EXISTS idx_wallpapers_type ON wallpapers(type)");
  exec("CREATE INDEX IF NOT EXISTS idx_wallpapers_color ON wallpapers(color_group)");
  exec("CREATE INDEX IF NOT EXISTS idx_wallpapers_favorite ON wallpapers(favorite)");
}

void Database::upsert_wallpaper(const Wallpaper &wallpaper) {
  Statement stmt(db_, "INSERT INTO wallpapers "
                      "(path,name,type,thumb_path,dominant_hex,hue,saturation,value,color_group,"
                      "favorite,width,height,added_at,last_used_at) "
                      "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
                      "ON CONFLICT(path) DO UPDATE SET "
                      "name=excluded.name,type=excluded.type,thumb_path=excluded.thumb_path,"
                      "dominant_hex=excluded.dominant_hex,hue=excluded.hue,"
                      "saturation=excluded.saturation,value=excluded.value,"
                      "color_group=excluded.color_group,width=excluded.width,height=excluded.height");
  sqlite3_bind_text(stmt.get(), 1, wallpaper.path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 2, wallpaper.name.c_str(), -1, SQLITE_TRANSIENT);
  const std::string type = to_string(wallpaper.type);
  sqlite3_bind_text(stmt.get(), 3, type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 4, wallpaper.thumb_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 5, wallpaper.dominant_hex.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt.get(), 6, wallpaper.hue);
  sqlite3_bind_double(stmt.get(), 7, wallpaper.saturation);
  sqlite3_bind_double(stmt.get(), 8, wallpaper.value);
  sqlite3_bind_int(stmt.get(), 9, static_cast<int>(wallpaper.color_group));
  sqlite3_bind_int(stmt.get(), 10, wallpaper.favorite ? 1 : 0);
  sqlite3_bind_int(stmt.get(), 11, wallpaper.width);
  sqlite3_bind_int(stmt.get(), 12, wallpaper.height);
  sqlite3_bind_int64(stmt.get(), 13, wallpaper.added_at);
  if (wallpaper.last_used_at.has_value()) {
    sqlite3_bind_int64(stmt.get(), 14, *wallpaper.last_used_at);
  } else {
    sqlite3_bind_null(stmt.get(), 14);
  }
  stmt.step();
}

std::vector<Wallpaper> Database::list_wallpapers(const Filter &filter) const {
  Statement stmt(db_, "SELECT id,path,name,type,thumb_path,dominant_hex,hue,saturation,value,"
                      "color_group,favorite,width,height,added_at,last_used_at "
                      "FROM wallpapers ORDER BY COALESCE(last_used_at, added_at) DESC, name ASC");
  std::vector<Wallpaper> out;
  while (stmt.step()) {
    auto item = row_to_wallpaper(stmt.get());
    if (matches_filter(item, tags_for_wallpaper(item.id), filter)) {
      out.push_back(std::move(item));
    }
  }
  return out;
}

std::optional<Wallpaper> Database::find_by_path(const std::string &path) const {
  Statement stmt(db_, "SELECT id,path,name,type,thumb_path,dominant_hex,hue,saturation,value,"
                      "color_group,favorite,width,height,added_at,last_used_at "
                      "FROM wallpapers WHERE path=?");
  sqlite3_bind_text(stmt.get(), 1, path.c_str(), -1, SQLITE_TRANSIENT);
  if (stmt.step()) {
    return row_to_wallpaper(stmt.get());
  }
  return std::nullopt;
}

std::optional<Wallpaper> Database::random_wallpaper() const {
  Statement stmt(db_, "SELECT id,path,name,type,thumb_path,dominant_hex,hue,saturation,value,"
                      "color_group,favorite,width,height,added_at,last_used_at "
                      "FROM wallpapers ORDER BY random() LIMIT 1");
  if (stmt.step()) {
    return row_to_wallpaper(stmt.get());
  }
  return std::nullopt;
}

std::optional<int> Database::wallpaper_id_for_path(const std::string &path) const {
  Statement stmt(db_, "SELECT id FROM wallpapers WHERE path=?");
  sqlite3_bind_text(stmt.get(), 1, path.c_str(), -1, SQLITE_TRANSIENT);
  if (stmt.step()) {
    return sqlite3_column_int(stmt.get(), 0);
  }
  return std::nullopt;
}

void Database::set_favorite(const std::string &path, bool favorite) {
  Statement stmt(db_, "UPDATE wallpapers SET favorite=? WHERE path=?");
  sqlite3_bind_int(stmt.get(), 1, favorite ? 1 : 0);
  sqlite3_bind_text(stmt.get(), 2, path.c_str(), -1, SQLITE_TRANSIENT);
  stmt.step();
}

void Database::add_tag(const std::string &path, const std::string &tag) {
  const auto wallpaper_id = wallpaper_id_for_path(path);
  if (!wallpaper_id.has_value()) {
    throw std::runtime_error("unknown wallpaper path: " + path);
  }
  {
    Statement stmt(db_, "INSERT OR IGNORE INTO tags(name) VALUES(?)");
    sqlite3_bind_text(stmt.get(), 1, tag.c_str(), -1, SQLITE_TRANSIENT);
    stmt.step();
  }
  Statement stmt(db_, "INSERT OR IGNORE INTO wallpaper_tags(wallpaper_id, tag_id) "
                      "SELECT ?, id FROM tags WHERE name=?");
  sqlite3_bind_int(stmt.get(), 1, *wallpaper_id);
  sqlite3_bind_text(stmt.get(), 2, tag.c_str(), -1, SQLITE_TRANSIENT);
  stmt.step();
}

void Database::remove_tag(const std::string &path, const std::string &tag) {
  const auto wallpaper_id = wallpaper_id_for_path(path);
  if (!wallpaper_id.has_value()) {
    throw std::runtime_error("unknown wallpaper path: " + path);
  }
  Statement stmt(db_, "DELETE FROM wallpaper_tags WHERE wallpaper_id=? AND tag_id IN "
                      "(SELECT id FROM tags WHERE name=?)");
  sqlite3_bind_int(stmt.get(), 1, *wallpaper_id);
  sqlite3_bind_text(stmt.get(), 2, tag.c_str(), -1, SQLITE_TRANSIENT);
  stmt.step();
}

std::vector<std::string> Database::tags_for_wallpaper(int wallpaper_id) const {
  Statement stmt(db_, "SELECT tags.name FROM tags "
                      "JOIN wallpaper_tags ON wallpaper_tags.tag_id=tags.id "
                      "WHERE wallpaper_tags.wallpaper_id=? ORDER BY tags.name");
  sqlite3_bind_int(stmt.get(), 1, wallpaper_id);
  std::vector<std::string> out;
  while (stmt.step()) {
    out.push_back(column_text(stmt.get(), 0));
  }
  return out;
}

void Database::set_setting(const std::string &key, const std::string &value) {
  Statement stmt(db_, "INSERT INTO settings(key,value) VALUES(?,?) "
                      "ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  sqlite3_bind_text(stmt.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 2, value.c_str(), -1, SQLITE_TRANSIENT);
  stmt.step();
}

std::optional<std::string> Database::setting(const std::string &key) const {
  Statement stmt(db_, "SELECT value FROM settings WHERE key=?");
  sqlite3_bind_text(stmt.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
  if (stmt.step()) {
    return column_text(stmt.get(), 0);
  }
  return std::nullopt;
}

void Database::cache_wallhaven(const WallhavenEntry &entry) {
  Statement stmt(db_, "INSERT INTO wallhaven_cache "
                      "(id,page_url,image_url,preview_url,thumb_path,width,height,purity,"
                      "category,colors,cached_at) VALUES (?,?,?,?,?,?,?,?,?,?,?) "
                      "ON CONFLICT(id) DO UPDATE SET "
                      "page_url=excluded.page_url,image_url=excluded.image_url,"
                      "preview_url=excluded.preview_url,thumb_path=excluded.thumb_path,"
                      "width=excluded.width,height=excluded.height,purity=excluded.purity,"
                      "category=excluded.category,colors=excluded.colors,cached_at=excluded.cached_at");
  sqlite3_bind_text(stmt.get(), 1, entry.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 2, entry.page_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 3, entry.image_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 4, entry.preview_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 5, entry.thumb_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt.get(), 6, entry.width);
  sqlite3_bind_int(stmt.get(), 7, entry.height);
  sqlite3_bind_text(stmt.get(), 8, entry.purity.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 9, entry.category.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.get(), 10, entry.colors_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt.get(), 11, entry.cached_at);
  stmt.step();
}

std::vector<WallhavenEntry> Database::cached_wallhaven() const {
  Statement stmt(db_, "SELECT id,page_url,image_url,preview_url,COALESCE(thumb_path,''),"
                      "COALESCE(width,0),COALESCE(height,0),COALESCE(purity,''),"
                      "COALESCE(category,''),COALESCE(colors,''),cached_at "
                      "FROM wallhaven_cache ORDER BY cached_at DESC");
  std::vector<WallhavenEntry> out;
  while (stmt.step()) {
    WallhavenEntry entry;
    entry.id = column_text(stmt.get(), 0);
    entry.page_url = column_text(stmt.get(), 1);
    entry.image_url = column_text(stmt.get(), 2);
    entry.preview_url = column_text(stmt.get(), 3);
    entry.thumb_path = column_text(stmt.get(), 4);
    entry.width = sqlite3_column_int(stmt.get(), 5);
    entry.height = sqlite3_column_int(stmt.get(), 6);
    entry.purity = column_text(stmt.get(), 7);
    entry.category = column_text(stmt.get(), 8);
    entry.colors_json = column_text(stmt.get(), 9);
    entry.cached_at = sqlite3_column_int64(stmt.get(), 10);
    out.push_back(std::move(entry));
  }
  return out;
}

} // namespace vibewall
