#include "core/wallhaven_client.hpp"

#include "core/app_paths.hpp"

#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace vibewall {

namespace {
constexpr const char *kUserAgent =
    "vibewall/0.1 (Wayland native wallpaper picker; +https://github.com/Valo-Asura/vibewall)";

size_t write_string(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *out = static_cast<std::string *>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t write_file(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *out = static_cast<std::ofstream *>(userdata);
  out->write(ptr, static_cast<std::streamsize>(size * nmemb));
  return size * nmemb;
}

void curl_download(const std::string &url, const std::filesystem::path &output, long timeout) {
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }
  ensure_parent_dir(output);
  std::ofstream file(output, std::ios::binary);
  if (!file) {
    curl_easy_cleanup(curl);
    throw std::runtime_error("failed to open " + output.string());
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
  CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  char *content_type = nullptr;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
  const std::string content_type_value = content_type == nullptr ? "" : content_type;
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK) {
    std::error_code ec;
    std::filesystem::remove(output, ec);
    throw std::runtime_error("download failed: " + std::string(curl_easy_strerror(rc)));
  }
  if (status >= 400) {
    std::error_code ec;
    std::filesystem::remove(output, ec);
    throw std::runtime_error("download HTTP error: " + std::to_string(status));
  }
  if (!content_type_value.empty() && content_type_value.rfind("image/", 0) != 0) {
    std::error_code ec;
    std::filesystem::remove(output, ec);
    throw std::runtime_error("download returned non-image content");
  }
}

std::string curl_get(const std::string &url, const std::string &api_key = {}) {
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }
  std::string body;
  struct curl_slist *headers = nullptr;
  if (!api_key.empty()) {
    headers = curl_slist_append(headers, ("X-API-Key: " + api_key).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (headers != nullptr) {
    curl_slist_free_all(headers);
  }
  curl_easy_cleanup(curl);
  if (rc != CURLE_OK) {
    throw std::runtime_error("Wallhaven request failed: " + std::string(curl_easy_strerror(rc)));
  }
  if (status >= 400) {
    throw std::runtime_error("Wallhaven HTTP error: " + std::to_string(status));
  }
  return body;
}

std::string url_escape(const std::string &value) {
  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }
  char *escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
  if (escaped == nullptr) {
    curl_easy_cleanup(curl);
    throw std::runtime_error("curl_easy_escape failed");
  }
  std::string out = escaped;
  curl_free(escaped);
  curl_easy_cleanup(curl);
  return out;
}
} // namespace

std::vector<WallhavenEntry> parse_wallhaven_response(const std::string &json_text) {
  const auto parsed = nlohmann::json::parse(json_text);
  std::vector<WallhavenEntry> out;
  if (!parsed.contains("data") || !parsed["data"].is_array()) {
    return out;
  }
  for (const auto &item : parsed["data"]) {
    WallhavenEntry entry;
    entry.id = item.value("id", "");
    entry.page_url = item.value("url", "");
    entry.image_url = item.value("path", "");
    entry.width = item.value("dimension_x", 0);
    entry.height = item.value("dimension_y", 0);
    entry.purity = item.value("purity", "");
    entry.category = item.value("category", "");
    if (item.contains("thumbs")) {
      entry.preview_url = item["thumbs"].value(
          "original", item["thumbs"].value("large", item["thumbs"].value("small", "")));
    }
    if (item.contains("colors")) {
      entry.colors_json = item["colors"].dump();
    }
    entry.cached_at = unix_time_now();
    if (!entry.id.empty() && !entry.image_url.empty()) {
      out.push_back(std::move(entry));
    }
  }
  return out;
}

std::vector<WallhavenEntry> wallhaven_search(const AppConfig &config, const std::string &query,
                                             int page) {
  const int safe_page = page <= 0 ? 1 : page;
  const std::string url = "https://wallhaven.cc/api/v1/search?q=" + url_escape(query) +
                          "&page=" + std::to_string(safe_page) +
                          "&categories=111&purity=100&sorting=toplist&topRange=1M";
  return parse_wallhaven_response(curl_get(url, config.wallhaven_api_key));
}

std::filesystem::path wallhaven_download(const AppConfig &config, const WallhavenEntry &entry) {
  if (entry.image_url.empty() || entry.id.empty()) {
    throw std::runtime_error("Wallhaven entry is missing URL or id");
  }
  ensure_dir(config.wallpaper_dir);
  const std::filesystem::path ext = std::filesystem::path(entry.image_url).extension();
  const auto output = config.wallpaper_dir / ("wallhaven-" + entry.id + ext.string());

  if (!std::filesystem::exists(output)) {
    curl_download(entry.image_url, output, 60L);
  }
  return output;
}

std::filesystem::path wallhaven_download_preview(const AppConfig &config,
                                                 const WallhavenEntry &entry) {
  if (entry.preview_url.empty() || entry.id.empty()) {
    throw std::runtime_error("Wallhaven entry is missing preview URL or id");
  }
  const auto thumb_dir = config.cache_dir / "wallhaven";
  ensure_dir(thumb_dir);
  std::filesystem::path ext = std::filesystem::path(entry.preview_url).extension();
  if (ext.empty()) {
    ext = ".jpg";
  }
  const auto output = thumb_dir / (entry.id + "-q2" + ext.string());
  if (!std::filesystem::exists(output)) {
    curl_download(entry.preview_url, output, 20L);
  }
  return output;
}

void cache_wallhaven_search(Database &db, const AppConfig &config, const std::string &query,
                            int page) {
  for (auto entry : wallhaven_search(config, query, page)) {
    try {
      entry.thumb_path = wallhaven_download_preview(config, entry).string();
    } catch (const std::exception &) {
      entry.thumb_path.clear();
    }
    db.cache_wallhaven(entry);
  }
}

} // namespace vibewall
