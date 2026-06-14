#pragma once

#include "core/model.hpp"

#include <GLES2/gl2.h>
#include <cstddef>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace vibewall::picker {

struct Point {
  float x = 0.0F;
  float y = 0.0F;
};

struct HitRegion {
  int index = -1;
  std::vector<Point> polygon;
};

enum class PickerAction {
  None,
  Slice,
  Grid,
  Hex,
  Mosaic,
  Wallhaven,
  Local,
  Random,
  Search,
  Download,
  Apply,
  FavoriteFilter,
  TypeAll,
  TypePic,
  TypeVid,
  TypeWeb,
  ColorAll,
  ColorRed,
  ColorOrange,
  ColorYellow,
  ColorLime,
  ColorGreen,
  ColorCyan,
  ColorBlue,
  ColorPurple,
  ColorPink,
  ColorBrown,
  ColorWhite,
  ColorGray,
  ColorBlack,
  // Wallhaven sort/filter tabs
  WallhavenTrend,
  WallhavenNew,
  WallhavenTop,
  WallhavenPopular,
  WallhavenSFW,
  WallhavenNextPage,
};

struct ActionRegion {
  PickerAction action = PickerAction::None;
  float x = 0.0F;
  float y = 0.0F;
  float w = 0.0F;
  float h = 0.0F;
};

struct FavoriteRegion {
  int index = -1;
  float x = 0.0F;
  float y = 0.0F;
  float w = 0.0F;
  float h = 0.0F;
};

struct TextureInfo {
  GLuint id = 0;
  int width = 0;
  int height = 0;
};

class Renderer {
public:
  Renderer() = default;
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  Renderer(Renderer &&) = delete;
  Renderer &operator=(Renderer &&) = delete;

  void init();
  void shutdown();
  void resize(int width, int height);
  void render(const std::vector<Wallpaper> &wallpapers, int selected, int scroll_offset, DisplayMode mode,
              const std::string &query, bool wallhaven_mode, const std::string &status,
              const std::optional<WallpaperType> &type_filter,
              const std::optional<ColorGroup> &color_filter, bool favorites_only,
              const std::string &background_path,
              const std::string &sort = "toplist", bool sfw = true);
  int hit_test(float x, float y) const;
  int favorite_hit_test(float x, float y) const;
  PickerAction action_hit_test(float x, float y) const;
  bool stage_contains(float x, float y) const;

private:
  int width_ = 1;
  int height_ = 1;
  GLuint program_ = 0;
  GLint pos_loc_ = -1;
  GLint uv_loc_ = -1;
  GLint color_loc_ = -1;
  GLint use_texture_loc_ = -1;
  GLint texture_loc_ = -1;
  std::map<std::string, TextureInfo> textures_;
  std::list<std::string> texture_lru_;
  std::vector<HitRegion> hit_regions_;
  std::vector<ActionRegion> action_regions_;
  std::vector<FavoriteRegion> favorite_regions_;
  float stage_x_ = 0.0F;
  float stage_y_ = 0.0F;
  float stage_w_ = 1.0F;
  float stage_h_ = 1.0F;
  float content_x_ = 0.0F;
  float content_y_ = 0.0F;
  float content_w_ = 1.0F;
  float content_h_ = 1.0F;

  TextureInfo texture_for(const Wallpaper &wallpaper);
  TextureInfo texture_for_path(const std::string &path);
  void clear_textures();
  void touch_texture(const std::string &path);
  void evict_textures_if_needed();
  void update_stage(DisplayMode mode);
  void draw_background(const std::string &background_path);
  void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a);
  void draw_textured_quad(float x, float y, float w, float h, GLuint texture, float alpha,
                          float u0 = 0.0F, float v0 = 0.0F, float u1 = 1.0F, float v1 = 1.0F);
  void draw_wallpaper_preview(float x, float y, float w, float h, const Wallpaper &wallpaper,
                              float alpha);
  void draw_wallpaper_polygon(const std::vector<Point> &points, const Wallpaper &wallpaper,
                              float alpha);
  void draw_polygon(const std::vector<Point> &points, float r, float g, float b, float a);
  void draw_textured_polygon(const std::vector<Point> &points, GLuint texture, float alpha,
                             float u0 = 0.0F, float v0 = 0.0F, float u1 = 1.0F,
                             float v1 = 1.0F);
  void draw_text(float x, float y, const std::string &text, float scale, float r, float g, float b,
                 float a);
  void draw_dot_text(float x, float y, const std::string &text, float scale, float r, float g,
                     float b, float a);
  float draw_badge(float x, float y, const std::string &label, bool active);
  float draw_chip(float x, float y, const std::string &label, bool active, float r, float g,
                  float b);
  void draw_tiny_badge(float x, float y, const std::string &label, float r, float g, float b);
  void draw_wallpaper_badges(float x, float y, float w, float h, const Wallpaper &wallpaper,
                             bool selected, int index);
  void render_toolbar(const std::vector<Wallpaper> &wallpapers, DisplayMode mode,
                      const std::string &query, bool wallhaven_mode, const std::string &status,
                      const std::optional<WallpaperType> &type_filter,
                      const std::optional<ColorGroup> &color_filter, bool favorites_only,
                      const std::string &sort, bool sfw);
  void add_action_region(PickerAction action, float x, float y, float w, float h);
  void render_grid(const std::vector<Wallpaper> &wallpapers, int selected, int scroll_offset);
  void render_slice(const std::vector<Wallpaper> &wallpapers, int selected);
  void render_hex(const std::vector<Wallpaper> &wallpapers, int selected, int scroll_offset);
  void render_mosaic(const std::vector<Wallpaper> &wallpapers, int selected, int scroll_offset);
};

} // namespace vibewall::picker
