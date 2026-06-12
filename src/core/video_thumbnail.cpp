#include "core/video_thumbnail.hpp"

#include "core/app_paths.hpp"
#include "core/process.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace vibewall {

namespace {
std::string lower_ext(const std::filesystem::path &path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext;
}
} // namespace

bool is_video_extension(const std::filesystem::path &path) {
  const std::string ext = lower_ext(path);
  return ext == ".mp4" || ext == ".webm" || ext == ".mkv" || ext == ".mov";
}

bool is_image_extension(const std::filesystem::path &path) {
  const std::string ext = lower_ext(path);
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" ||
         ext == ".bmp" || ext == ".avif";
}

ThumbnailInfo create_video_thumbnail(const AppConfig &config, const std::filesystem::path &source) {
  const auto thumb_dir = config.cache_dir / "thumbs";
  ensure_dir(thumb_dir);
  const auto thumb_path = thumb_dir / (fnv1a_hex(source.string() + "|q2-960") + ".png");
  if (!std::filesystem::exists(thumb_path)) {
    const auto result = run_process({"ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                                     "-ss", "00:00:00.1", "-i", source.string(), "-frames:v", "1",
                                     "-vf", "scale=min(960\\,iw):-2,format=rgba", "-update", "1",
                                     thumb_path.string()});
    if (result.exit_code != 0 || !std::filesystem::exists(thumb_path)) {
      throw std::runtime_error("ffmpeg thumbnail failed for " + source.string());
    }
  }
  return analyze_thumbnail(thumb_path);
}

} // namespace vibewall
