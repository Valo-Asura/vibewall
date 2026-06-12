#include "core/thumbnail_cache.hpp"

#include "core/app_paths.hpp"

#include <stdexcept>
#include <vips/vips8>

namespace vibewall {

namespace {
void init_vips_once() {
  static bool initialized = [] {
    if (VIPS_INIT("vibewallREzero") != 0) {
      throw std::runtime_error("failed to initialize libvips");
    }
    return true;
  }();
  (void)initialized;
}

Rgb average_rgb(const vips::VImage &image) {
  vips::VImage sample = image.thumbnail_image(64, vips::VImage::option()->set("height", 64));
  if (sample.bands() > 3) {
    sample = sample.extract_band(0, vips::VImage::option()->set("n", 3));
  }
  sample = sample.colourspace(VIPS_INTERPRETATION_sRGB);
  double avg[3] = {0.0, 0.0, 0.0};
  const int width = sample.width();
  const int height = sample.height();
  size_t bytes = 0;
  void *memory = sample.write_to_memory(&bytes);
  if (memory == nullptr || bytes == 0) {
    throw std::runtime_error("failed to read thumbnail pixels");
  }
  const auto *pixels = static_cast<unsigned char *>(memory);
  const int bands = sample.bands();
  const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  for (std::size_t i = 0; i < count; ++i) {
    avg[0] += pixels[i * bands + 0];
    avg[1] += pixels[i * bands + 1];
    avg[2] += pixels[i * bands + 2];
  }
  g_free(memory);
  if (count == 0) {
    return {};
  }
  return {static_cast<std::uint8_t>(avg[0] / count), static_cast<std::uint8_t>(avg[1] / count),
          static_cast<std::uint8_t>(avg[2] / count)};
}
} // namespace

ThumbnailInfo create_image_thumbnail(const AppConfig &config, const std::filesystem::path &source) {
  init_vips_once();
  const auto thumb_dir = config.cache_dir / "thumbs";
  ensure_dir(thumb_dir);
  const auto thumb_path = thumb_dir / (fnv1a_hex(source.string() + "|q2-960") + ".webp");

  vips::VImage original = vips::VImage::new_from_file(
      source.c_str(), vips::VImage::option()->set("access", "sequential"));
  const int source_width = original.width();
  const int source_height = original.height();
  if (!std::filesystem::exists(thumb_path)) {
    vips::VImage thumb = original.thumbnail_image(960, vips::VImage::option()->set("height", 540));
    thumb.write_to_file(thumb_path.c_str(), vips::VImage::option()->set("Q", 86));
  }
  vips::VImage thumb_for_average = vips::VImage::new_from_file(thumb_path.c_str());
  return {thumb_path, average_rgb(thumb_for_average), source_width, source_height};
}

ThumbnailInfo analyze_thumbnail(const std::filesystem::path &thumb_path) {
  init_vips_once();
  vips::VImage image = vips::VImage::new_from_file(thumb_path.c_str());
  return {thumb_path, average_rgb(image), image.width(), image.height()};
}

} // namespace vibewall
