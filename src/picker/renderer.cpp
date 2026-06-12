#include "picker/renderer.hpp"

#include "core/color_sort.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vips/vips8>

namespace vibewall::picker {

namespace {
void init_vips_once() {
  static bool initialized = [] {
    if (VIPS_INIT("vibewallREzero-picker") != 0) {
      throw std::runtime_error("failed to initialize libvips");
    }
    return true;
  }();
  (void)initialized;
}

GLuint compile_shader(GLenum type, const char *source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    throw std::runtime_error("shader compile failed: " + std::string(log));
  }
  return shader;
}

GLuint create_program() {
  static constexpr const char *vertex = R"GLSL(
    attribute vec2 a_pos;
    attribute vec2 a_uv;
    varying vec2 v_uv;
    void main() {
      v_uv = a_uv;
      gl_Position = vec4(a_pos, 0.0, 1.0);
    }
  )GLSL";
  static constexpr const char *fragment = R"GLSL(
    precision mediump float;
    varying vec2 v_uv;
    uniform sampler2D u_texture;
    uniform vec4 u_color;
    uniform int u_use_texture;
    void main() {
      vec4 base = u_use_texture == 1 ? texture2D(u_texture, v_uv) : vec4(1.0);
      gl_FragColor = base * u_color;
    }
  )GLSL";
  const GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex);
  const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    throw std::runtime_error("program link failed: " + std::string(log));
  }
  return program;
}

float ndc_x(float x, int width) {
  return (x / static_cast<float>(width)) * 2.0F - 1.0F;
}

float ndc_y(float y, int height) {
  return 1.0F - (y / static_cast<float>(height)) * 2.0F;
}

std::array<float, 4> color_from_group(ColorGroup group) {
  switch (group) {
  case ColorGroup::Red:
    return {0.95F, 0.22F, 0.25F, 1.0F};
  case ColorGroup::Orange:
    return {0.95F, 0.50F, 0.18F, 1.0F};
  case ColorGroup::Yellow:
    return {0.90F, 0.78F, 0.18F, 1.0F};
  case ColorGroup::Lime:
    return {0.55F, 0.85F, 0.20F, 1.0F};
  case ColorGroup::Green:
    return {0.20F, 0.75F, 0.38F, 1.0F};
  case ColorGroup::Cyan:
    return {0.15F, 0.75F, 0.85F, 1.0F};
  case ColorGroup::Blue:
    return {0.25F, 0.45F, 0.95F, 1.0F};
  case ColorGroup::Purple:
    return {0.58F, 0.38F, 0.92F, 1.0F};
  case ColorGroup::Pink:
    return {0.92F, 0.35F, 0.75F, 1.0F};
  case ColorGroup::Brown:
    return {0.56F, 0.36F, 0.20F, 1.0F};
  case ColorGroup::White:
    return {0.92F, 0.92F, 0.86F, 1.0F};
  case ColorGroup::Gray:
    return {0.42F, 0.46F, 0.52F, 1.0F};
  case ColorGroup::Black:
    return {0.12F, 0.13F, 0.16F, 1.0F};
  }
  return {0.2F, 0.2F, 0.2F, 1.0F};
}

bool point_in_polygon(float x, float y, const std::vector<Point> &points) {
  bool inside = false;
  for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
    const bool intersect = ((points[i].y > y) != (points[j].y > y)) &&
                           (x < (points[j].x - points[i].x) * (y - points[i].y) /
                                            (points[j].y - points[i].y + 0.0001F) +
                                        points[i].x);
    if (intersect) {
      inside = !inside;
    }
  }
  return inside;
}

std::string short_name(const Wallpaper &wallpaper, std::size_t limit) {
  std::string name = wallpaper.name.empty() ? std::filesystem::path(wallpaper.path).filename().string()
                                            : wallpaper.name;
  if (name.size() <= limit) {
    return name;
  }
  return name.substr(0, limit > 2 ? limit - 2 : limit) + "..";
}

std::string type_label(WallpaperType type) {
  if (type == WallpaperType::Video) {
    return "VID";
  }
  if (type == WallpaperType::Wallhaven) {
    return "WEB";
  }
  return "PIC";
}

std::string color_label(ColorGroup group) {
  switch (group) {
  case ColorGroup::Red:
    return "RED";
  case ColorGroup::Orange:
    return "ORG";
  case ColorGroup::Yellow:
    return "YLW";
  case ColorGroup::Lime:
    return "LIM";
  case ColorGroup::Green:
    return "GRN";
  case ColorGroup::Cyan:
    return "CYN";
  case ColorGroup::Blue:
    return "BLU";
  case ColorGroup::Purple:
    return "PUR";
  case ColorGroup::Pink:
    return "PNK";
  case ColorGroup::Brown:
    return "BRN";
  case ColorGroup::White:
    return "WHT";
  case ColorGroup::Gray:
    return "GRY";
  case ColorGroup::Black:
    return "BLK";
  }
  return "BLK";
}

PickerAction color_action(ColorGroup group) {
  switch (group) {
  case ColorGroup::Red:
    return PickerAction::ColorRed;
  case ColorGroup::Orange:
    return PickerAction::ColorOrange;
  case ColorGroup::Yellow:
    return PickerAction::ColorYellow;
  case ColorGroup::Lime:
    return PickerAction::ColorLime;
  case ColorGroup::Green:
    return PickerAction::ColorGreen;
  case ColorGroup::Cyan:
    return PickerAction::ColorCyan;
  case ColorGroup::Blue:
    return PickerAction::ColorBlue;
  case ColorGroup::Purple:
    return PickerAction::ColorPurple;
  case ColorGroup::Pink:
    return PickerAction::ColorPink;
  case ColorGroup::Brown:
    return PickerAction::ColorBrown;
  case ColorGroup::White:
    return PickerAction::ColorWhite;
  case ColorGroup::Gray:
    return PickerAction::ColorGray;
  case ColorGroup::Black:
    return PickerAction::ColorBlack;
  }
  return PickerAction::ColorBlack;
}

std::vector<ColorGroup> color_groups() {
  return {ColorGroup::Red,    ColorGroup::Orange, ColorGroup::Yellow, ColorGroup::Lime,
          ColorGroup::Green,  ColorGroup::Cyan,   ColorGroup::Blue,   ColorGroup::Purple,
          ColorGroup::Pink,   ColorGroup::Brown,  ColorGroup::White,  ColorGroup::Gray,
          ColorGroup::Black};
}

std::array<std::uint8_t, 7> glyph(char c) {
  switch (c) {
  case 'A': return {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
  case 'B': return {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
  case 'C': return {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F};
  case 'D': return {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
  case 'E': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
  case 'F': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
  case 'G': return {0x0F,0x10,0x10,0x13,0x11,0x11,0x0F};
  case 'H': return {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
  case 'I': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F};
  case 'J': return {0x01,0x01,0x01,0x01,0x11,0x11,0x0E};
  case 'K': return {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
  case 'L': return {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
  case 'M': return {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
  case 'N': return {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
  case 'O': return {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
  case 'P': return {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
  case 'Q': return {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};
  case 'R': return {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
  case 'S': return {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
  case 'T': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
  case 'U': return {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
  case 'V': return {0x11,0x11,0x11,0x11,0x11,0x0A,0x04};
  case 'W': return {0x11,0x11,0x11,0x15,0x15,0x1B,0x11};
  case 'X': return {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11};
  case 'Y': return {0x11,0x11,0x0A,0x04,0x04,0x04,0x04};
  case 'Z': return {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F};
  case '0': return {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
  case '1': return {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
  case '2': return {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
  case '3': return {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
  case '4': return {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
  case '5': return {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
  case '6': return {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E};
  case '7': return {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
  case '8': return {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
  case '9': return {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};
  case '-': return {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
  case '_': return {0x00,0x00,0x00,0x00,0x00,0x00,0x1F};
  case '.': return {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
  case '/': return {0x01,0x01,0x02,0x04,0x08,0x10,0x10};
  case ':': return {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00};
  default: return {0x00,0x00,0x00,0x00,0x00,0x00,0x00};
  }
}
} // namespace

void Renderer::init() {
  init_vips_once();
  program_ = create_program();
  pos_loc_ = glGetAttribLocation(program_, "a_pos");
  uv_loc_ = glGetAttribLocation(program_, "a_uv");
  color_loc_ = glGetUniformLocation(program_, "u_color");
  use_texture_loc_ = glGetUniformLocation(program_, "u_use_texture");
  texture_loc_ = glGetUniformLocation(program_, "u_texture");
  glUseProgram(program_);
  glUniform1i(texture_loc_, 0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::resize(int width, int height) {
  width_ = std::max(1, width);
  height_ = std::max(1, height);
  glViewport(0, 0, width_, height_);
}

void Renderer::update_stage(DisplayMode mode) {
  const float screen_w = static_cast<float>(width_);
  const float screen_h = static_cast<float>(height_);
  stage_x_ = 0.0F;
  stage_y_ = 0.0F;
  stage_w_ = screen_w;
  stage_h_ = screen_h;

  const float side_margin = std::clamp(screen_w * 0.075F, 52.0F, 140.0F);
  float max_content_w = 1320.0F;
  content_y_ = 150.0F;
  if (mode == DisplayMode::Slice) {
    max_content_w = 1540.0F;
    content_y_ = std::max(150.0F, screen_h * 0.23F);
  } else if (mode == DisplayMode::Hex) {
    max_content_w = 1180.0F;
    content_y_ = 138.0F;
  } else if (mode == DisplayMode::Mosaic) {
    max_content_w = 1260.0F;
    content_y_ = 150.0F;
  }
  const float available_w = std::max(320.0F, screen_w - side_margin * 2.0F);
  content_w_ = std::min(available_w, max_content_w);
  content_x_ = (screen_w - content_w_) * 0.5F;
  content_h_ = std::max(240.0F, screen_h - content_y_ - 86.0F);
}

TextureInfo Renderer::texture_for(const Wallpaper &wallpaper) {
  return texture_for_path(wallpaper.thumb_path);
}

TextureInfo Renderer::texture_for_path(const std::string &path) {
  if (path.empty()) {
    return {};
  }
  if (auto it = textures_.find(path); it != textures_.end()) {
    return it->second;
  }
  try {
    vips::VImage image = vips::VImage::new_from_file(path.c_str());
    image = image.colourspace(VIPS_INTERPRETATION_sRGB);
    if (image.width() > 1080 || image.height() > 720) {
      image = image.thumbnail_image(1080, vips::VImage::option()->set("height", 720));
    }
    if (image.bands() == 3) {
      image = image.bandjoin(255);
    } else if (image.bands() > 4) {
      image = image.extract_band(0, vips::VImage::option()->set("n", 4));
    }
    size_t bytes = 0;
    void *memory = image.write_to_memory(&bytes);
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, memory);
    g_free(memory);
    TextureInfo info{
        .id = texture,
        .width = image.width(),
        .height = image.height(),
    };
    textures_[path] = info;
    return info;
  } catch (const std::exception &err) {
    std::cerr << "texture warning: " << path << ": " << err.what() << '\n';
    return {};
  }
}

void Renderer::draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
  const GLfloat vertices[] = {
      ndc_x(x, width_),     ndc_y(y, height_),     0.0F, 0.0F,
      ndc_x(x + w, width_), ndc_y(y, height_),     1.0F, 0.0F,
      ndc_x(x, width_),     ndc_y(y + h, height_), 0.0F, 1.0F,
      ndc_x(x + w, width_), ndc_y(y + h, height_), 1.0F, 1.0F,
  };
  glUseProgram(program_);
  glUniform4f(color_loc_, r, g, b, a);
  glUniform1i(use_texture_loc_, 0);
  glVertexAttribPointer(pos_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
  glVertexAttribPointer(uv_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);
  glEnableVertexAttribArray(pos_loc_);
  glEnableVertexAttribArray(uv_loc_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::draw_textured_quad(float x, float y, float w, float h, GLuint texture, float alpha,
                                  float u0, float v0, float u1, float v1) {
  if (texture == 0) {
    draw_rect(x, y, w, h, 0.10F, 0.12F, 0.15F, alpha);
    return;
  }
  const GLfloat vertices[] = {
      ndc_x(x, width_),     ndc_y(y, height_),     u0, v0,
      ndc_x(x + w, width_), ndc_y(y, height_),     u1, v0,
      ndc_x(x, width_),     ndc_y(y + h, height_), u0, v1,
      ndc_x(x + w, width_), ndc_y(y + h, height_), u1, v1,
  };
  glUseProgram(program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform4f(color_loc_, 1.0F, 1.0F, 1.0F, alpha);
  glUniform1i(use_texture_loc_, 1);
  glVertexAttribPointer(pos_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
  glVertexAttribPointer(uv_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);
  glEnableVertexAttribArray(pos_loc_);
  glEnableVertexAttribArray(uv_loc_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::draw_textured_polygon(const std::vector<Point> &points, GLuint texture, float alpha,
                                     float u0, float v0, float u1, float v1) {
  if (points.size() < 3) {
    return;
  }
  if (texture == 0) {
    draw_polygon(points, 0.10F, 0.12F, 0.15F, alpha);
    return;
  }
  float min_x = points.front().x;
  float min_y = points.front().y;
  float max_x = points.front().x;
  float max_y = points.front().y;
  for (const auto &point : points) {
    min_x = std::min(min_x, point.x);
    min_y = std::min(min_y, point.y);
    max_x = std::max(max_x, point.x);
    max_y = std::max(max_y, point.y);
  }
  const float box_w = std::max(1.0F, max_x - min_x);
  const float box_h = std::max(1.0F, max_y - min_y);
  std::vector<GLfloat> vertices;
  vertices.reserve(points.size() * 4);
  for (const auto &point : points) {
    const float px = (point.x - min_x) / box_w;
    const float py = (point.y - min_y) / box_h;
    vertices.push_back(ndc_x(point.x, width_));
    vertices.push_back(ndc_y(point.y, height_));
    vertices.push_back(u0 + px * (u1 - u0));
    vertices.push_back(v0 + py * (v1 - v0));
  }
  glUseProgram(program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform4f(color_loc_, 1.0F, 1.0F, 1.0F, alpha);
  glUniform1i(use_texture_loc_, 1);
  glVertexAttribPointer(pos_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices.data());
  glVertexAttribPointer(uv_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices.data() + 2);
  glEnableVertexAttribArray(pos_loc_);
  glEnableVertexAttribArray(uv_loc_);
  glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(points.size()));
}

void Renderer::draw_wallpaper_preview(float x, float y, float w, float h, const Wallpaper &wallpaper,
                                      float alpha) {
  const TextureInfo texture = texture_for(wallpaper);
  if (texture.id == 0 || texture.width <= 0 || texture.height <= 0) {
    draw_textured_quad(x, y, w, h, 0, alpha);
    draw_text(x + w * 0.5F - 36.0F, y + h * 0.5F - 8.0F, "NO PREVIEW", 1.7F, 0.88F, 0.92F,
              0.98F, 1.0F);
    return;
  }

  float u0 = 0.0F;
  float v0 = 0.0F;
  float u1 = 1.0F;
  float v1 = 1.0F;
  const float source_aspect = static_cast<float>(texture.width) / static_cast<float>(texture.height);
  const float target_aspect = w / h;
  if (source_aspect > target_aspect) {
    const float visible = target_aspect / source_aspect;
    const float crop = (1.0F - visible) * 0.5F;
    u0 = crop;
    u1 = 1.0F - crop;
  } else if (source_aspect < target_aspect) {
    const float visible = source_aspect / target_aspect;
    const float crop = (1.0F - visible) * 0.5F;
    v0 = crop;
    v1 = 1.0F - crop;
  }
  draw_textured_quad(x, y, w, h, texture.id, alpha, u0, v0, u1, v1);
}

void Renderer::draw_background(const std::string &background_path) {
  glDisable(GL_BLEND);
  draw_rect(0, 0, static_cast<float>(width_), static_cast<float>(height_), 0.006F, 0.014F,
            0.018F, 1.0F);
  glEnable(GL_BLEND);
  const TextureInfo texture = texture_for_path(background_path);
  if (texture.id == 0 || texture.width <= 0 || texture.height <= 0) {
    draw_rect(0, 0, static_cast<float>(width_), static_cast<float>(height_) * 0.55F, 0.025F,
              0.10F, 0.12F, 0.38F);
    return;
  }
  float u0 = 0.0F;
  float v0 = 0.0F;
  float u1 = 1.0F;
  float v1 = 1.0F;
  const float source_aspect = static_cast<float>(texture.width) / static_cast<float>(texture.height);
  const float target_aspect = static_cast<float>(width_) / static_cast<float>(height_);
  if (source_aspect > target_aspect) {
    const float visible = target_aspect / source_aspect;
    const float crop = (1.0F - visible) * 0.5F;
    u0 = crop;
    u1 = 1.0F - crop;
  } else if (source_aspect < target_aspect) {
    const float visible = source_aspect / target_aspect;
    const float crop = (1.0F - visible) * 0.5F;
    v0 = crop;
    v1 = 1.0F - crop;
  }
  draw_textured_quad(0, 0, static_cast<float>(width_), static_cast<float>(height_), texture.id,
                     1.0F, u0, v0, u1, v1);
}

void Renderer::draw_wallpaper_polygon(const std::vector<Point> &points, const Wallpaper &wallpaper,
                                      float alpha) {
  const TextureInfo texture = texture_for(wallpaper);
  if (texture.id == 0 || texture.width <= 0 || texture.height <= 0) {
    draw_polygon(points, 0.10F, 0.12F, 0.15F, alpha);
    return;
  }

  float min_x = points.front().x;
  float min_y = points.front().y;
  float max_x = points.front().x;
  float max_y = points.front().y;
  for (const auto &point : points) {
    min_x = std::min(min_x, point.x);
    min_y = std::min(min_y, point.y);
    max_x = std::max(max_x, point.x);
    max_y = std::max(max_y, point.y);
  }
  const float target_aspect = std::max(1.0F, max_x - min_x) / std::max(1.0F, max_y - min_y);
  const float source_aspect = static_cast<float>(texture.width) / static_cast<float>(texture.height);
  float u0 = 0.0F;
  float v0 = 0.0F;
  float u1 = 1.0F;
  float v1 = 1.0F;
  if (source_aspect > target_aspect) {
    const float visible = target_aspect / source_aspect;
    const float crop = (1.0F - visible) * 0.5F;
    u0 = crop;
    u1 = 1.0F - crop;
  } else if (source_aspect < target_aspect) {
    const float visible = source_aspect / target_aspect;
    const float crop = (1.0F - visible) * 0.5F;
    v0 = crop;
    v1 = 1.0F - crop;
  }
  draw_textured_polygon(points, texture.id, alpha, u0, v0, u1, v1);
}

void Renderer::draw_polygon(const std::vector<Point> &points, float r, float g, float b, float a) {
  std::vector<GLfloat> vertices;
  vertices.reserve(points.size() * 4);
  for (const auto &point : points) {
    vertices.push_back(ndc_x(point.x, width_));
    vertices.push_back(ndc_y(point.y, height_));
    vertices.push_back(0.5F);
    vertices.push_back(0.5F);
  }
  glUseProgram(program_);
  glUniform4f(color_loc_, r, g, b, a);
  glUniform1i(use_texture_loc_, 0);
  glVertexAttribPointer(pos_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices.data());
  glVertexAttribPointer(uv_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices.data() + 2);
  glEnableVertexAttribArray(pos_loc_);
  glEnableVertexAttribArray(uv_loc_);
  glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(points.size()));
}

void Renderer::draw_text(float x, float y, const std::string &text, float scale, float r, float g,
                         float b, float a) {
  float cursor = x;
  for (char raw : text) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
    const auto bits = glyph(c);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if ((bits[row] & (1 << (4 - col))) != 0) {
          if (a > 0.5F) {
            draw_rect(cursor + (col + 0.55F) * scale, y + (row + 0.55F) * scale, scale * 1.02F,
                      scale * 1.02F, 0.0F, 0.0F, 0.0F, std::min(0.58F, a));
          }
          draw_rect(cursor + col * scale, y + row * scale, scale * 1.02F, scale * 1.02F, r, g, b,
                    a);
        }
      }
    }
    cursor += scale * 6.0F;
    if (cursor > width_ - 20.0F) {
      return;
    }
  }
}

void Renderer::draw_dot_text(float x, float y, const std::string &text, float scale, float r,
                             float g, float b, float a) {
  float cursor = x;
  const float dot = std::max(1.0F, scale * 0.46F);
  for (char raw : text) {
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
    if (c == ' ') {
      cursor += scale * 3.8F;
      continue;
    }
    const auto bits = glyph(c);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if ((bits[row] & (1 << (4 - col))) != 0) {
          draw_rect(cursor + col * scale, y + row * scale, dot, dot, r, g, b, a);
        }
      }
    }
    cursor += scale * 6.5F;
    if (cursor > width_ - 20.0F) {
      return;
    }
  }
}

float Renderer::draw_badge(float x, float y, const std::string &label, bool active) {
  const float w = std::max(46.0F, static_cast<float>(label.size()) * 10.0F + 16.0F);
  draw_rect(x, y, w, 24.0F, active ? 0.35F : 0.10F, active ? 0.88F : 0.13F,
            active ? 0.68F : 0.16F, active ? 0.95F : 0.88F);
  draw_text(x + 8.0F, y + 7.0F, label, 1.6F, active ? 0.03F : 0.72F, active ? 0.05F : 0.76F,
            active ? 0.04F : 0.82F, 1.0F);
  return w;
}

float Renderer::draw_chip(float x, float y, const std::string &label, bool active, float r,
                          float g, float b) {
  const float w = std::max(38.0F, static_cast<float>(label.size()) * 9.0F + 18.0F);
  draw_rect(x, y, w, 24.0F, active ? r : 0.035F, active ? g : 0.055F,
            active ? b : 0.070F, active ? 0.95F : 0.72F);
  if (active) {
    draw_rect(x - 1.5F, y - 1.5F, w + 3.0F, 2.0F, 0.52F, 0.96F, 0.78F, 0.96F);
    draw_rect(x - 1.5F, y + 24.0F, w + 3.0F, 2.0F, 0.52F, 0.96F, 0.78F, 0.80F);
  }
  draw_text(x + 8.0F, y + 7.0F, label, 1.38F, active ? 0.02F : 0.72F,
            active ? 0.05F : 0.80F, active ? 0.04F : 0.86F, 1.0F);
  return w;
}

void Renderer::draw_tiny_badge(float x, float y, const std::string &label, float r, float g,
                               float b) {
  const float w = std::max(22.0F, static_cast<float>(label.size()) * 5.6F + 9.0F);
  draw_rect(x, y, w, 13.0F, 0.01F, 0.018F, 0.022F, 0.74F);
  draw_rect(x, y, 2.0F, 13.0F, r, g, b, 0.92F);
  draw_text(x + 5.0F, y + 4.0F, label, 0.82F, r, g, b, 1.0F);
}

void Renderer::draw_wallpaper_badges(float x, float y, float w, float h, const Wallpaper &wallpaper,
                                     bool selected) {
  draw_tiny_badge(x + 5.0F, y + h - 17.0F, type_label(wallpaper.type), 0.52F, 0.96F, 0.78F);
  if (wallpaper.type == WallpaperType::Video) {
    draw_tiny_badge(x + w - 34.0F, y + 5.0F, "PLAY", 0.52F, 0.96F, 0.78F);
  }
  if (wallpaper.favorite) {
    draw_tiny_badge(x + w - 34.0F, y + h - 17.0F, "FAV", 0.98F, 0.74F, 0.28F);
  }
  if (selected) {
    draw_tiny_badge(x + 5.0F, y + 5.0F, "CURRENT", 0.52F, 0.96F, 0.78F);
  } else if (wallpaper.type == WallpaperType::Wallhaven && !wallpaper.thumb_path.empty()) {
    draw_tiny_badge(x + 5.0F, y + 5.0F, "INSTALLED", 0.46F, 0.78F, 0.96F);
  }
}

void Renderer::render_toolbar(const std::vector<Wallpaper> &wallpapers, DisplayMode mode,
                              const std::string &query, bool wallhaven_mode,
                              const std::string &status,
                              const std::optional<WallpaperType> &type_filter,
                              const std::optional<ColorGroup> &color_filter,
                              bool favorites_only) {
  const float panel_w =
      std::min(static_cast<float>(width_) - 60.0F, wallhaven_mode ? 1280.0F : 1180.0F);
  const float panel_h = wallhaven_mode ? 106.0F : 90.0F;
  const float x = (static_cast<float>(width_) - panel_w) * 0.5F;
  const float y = 58.0F;
  draw_rect(x, y, panel_w, panel_h, 0.020F, 0.034F, 0.040F, 0.34F);
  draw_rect(x, y + panel_h - 2.0F, panel_w, 2.0F, 0.40F, 0.95F, 0.74F, 0.82F);

  draw_dot_text(x + 16.0F, y + 10.0F, "VIBEWALL  REZERO", 2.55F, 1.0F, 0.74F, 0.12F, 1.0F);
  if (!status.empty()) {
    draw_text(x + 330.0F, y + 12.0F, status.substr(0, 42), 1.45F, 0.72F, 0.82F, 0.92F, 1.0F);
  }
  const std::string count = std::to_string(wallpapers.size()) + " ITEMS";
  draw_text(x + panel_w - 130.0F, y + 12.0F, count, 1.35F, 0.72F, 0.82F, 0.92F, 1.0F);

  float cx = x + 14.0F;
  const float row = y + 40.0F;
  const auto chip = [&](const std::string &label, bool active, PickerAction action, float r,
                        float g, float b) {
    const float w = draw_chip(cx, row, label, active, r, g, b);
    add_action_region(action, cx, row, w, 24.0F);
    cx += w + 6.0F;
  };
  chip("SLICE", mode == DisplayMode::Slice, PickerAction::Slice, 0.52F, 0.96F, 0.78F);
  chip("GRID", mode == DisplayMode::Grid, PickerAction::Grid, 0.52F, 0.96F, 0.78F);
  chip("HEX", mode == DisplayMode::Hex, PickerAction::Hex, 0.52F, 0.96F, 0.78F);
  chip("MOSAIC", mode == DisplayMode::Mosaic, PickerAction::Mosaic, 0.52F, 0.96F, 0.78F);
  chip("WEB", wallhaven_mode, PickerAction::Wallhaven, 0.46F, 0.78F, 0.96F);
  chip("LOCAL", !wallhaven_mode, PickerAction::Local, 0.46F, 0.78F, 0.96F);
  chip("FAV", favorites_only, PickerAction::FavoriteFilter, 0.98F, 0.74F, 0.28F);

  cx += 8.0F;
  chip("ALL", !type_filter.has_value() && !wallhaven_mode, PickerAction::TypeAll, 0.52F, 0.96F,
       0.78F);
  chip("PIC", type_filter == WallpaperType::Image, PickerAction::TypePic, 0.52F, 0.96F, 0.78F);
  chip("VID", type_filter == WallpaperType::Video, PickerAction::TypeVid, 0.52F, 0.96F, 0.78F);
  chip("WEB", wallhaven_mode || type_filter == WallpaperType::Wallhaven, PickerAction::TypeWeb,
       0.46F, 0.78F, 0.96F);

  const float search_w = std::min(220.0F, std::max(132.0F, x + panel_w - cx - 328.0F));
  draw_rect(cx + 8.0F, row, search_w, 24.0F, 0.015F, 0.024F, 0.030F, 0.72F);
  draw_text(cx + 17.0F, row + 7.0F, query.empty() ? "/ SEARCH" : ("Q " + query).substr(0, 26),
            1.34F, 0.74F, 0.88F, 0.96F, 1.0F);
  add_action_region(PickerAction::Search, cx + 8.0F, row, search_w, 24.0F);
  cx += search_w + 18.0F;
  chip("DOWNLOAD", false, PickerAction::Download, 0.46F, 0.78F, 0.96F);
  chip("RAND", false, PickerAction::Random, 0.95F, 0.55F, 0.24F);
  chip("APPLY", false, PickerAction::Apply, 0.52F, 0.96F, 0.78F);

  float color_x = x + 14.0F;
  const float color_y = y + (wallhaven_mode ? 72.0F : 66.0F);
  const float all_w = draw_chip(color_x, color_y, "ALL", !color_filter.has_value(), 0.52F, 0.96F,
                                0.78F);
    add_action_region(PickerAction::ColorAll, color_x, color_y, all_w, 24.0F);
  color_x += all_w + 6.0F;
  for (ColorGroup group : color_groups()) {
    const auto color = color_from_group(group);
    const bool active = color_filter == group;
    const float block_w = 28.0F;
    draw_rect(color_x, color_y, block_w, 24.0F, color[0], color[1], color[2], 0.92F);
    if (active) {
      draw_rect(color_x - 2.0F, color_y - 2.0F, block_w + 4.0F, 2.0F, 0.52F, 0.96F, 0.78F,
                1.0F);
      draw_rect(color_x - 2.0F, color_y + 24.0F, block_w + 4.0F, 2.0F, 0.52F, 0.96F, 0.78F,
                0.92F);
    }
    add_action_region(color_action(group), color_x, color_y, block_w, 24.0F);
    color_x += block_w + 4.0F;
  }

  if (wallhaven_mode) {
    float wx = x + panel_w - 464.0F;
    const float wy = y + 72.0F;
    const auto tab = [&](const std::string &label, bool active) {
      const float w = draw_chip(wx, wy, label, active, 0.52F, 0.96F, 0.78F);
      wx += w + 5.0F;
    };
    tab("TREND", true);
    tab("NEW", false);
    tab("TOP", false);
    tab("POPULAR", false);
    tab("SFW", true);
    tab("1080P", false);
  }
}

void Renderer::add_action_region(PickerAction action, float x, float y, float w, float h) {
  action_regions_.push_back({
      .action = action,
      .x = x,
      .y = y,
      .w = w,
      .h = h,
  });
}

void Renderer::render(const std::vector<Wallpaper> &wallpapers, int selected, DisplayMode mode,
                      const std::string &query, bool wallhaven_mode, const std::string &status,
                      const std::optional<WallpaperType> &type_filter,
                      const std::optional<ColorGroup> &color_filter, bool favorites_only,
                      const std::string &background_path) {
  (void)background_path;
  hit_regions_.clear();
  action_regions_.clear();
  update_stage(mode);
  glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
  glClear(GL_COLOR_BUFFER_BIT);
  render_toolbar(wallpapers, mode, query, wallhaven_mode, status, type_filter, color_filter,
                 favorites_only);

  if (wallpapers.empty()) {
    draw_text(content_x_ + content_w_ * 0.18F, content_y_ + content_h_ * 0.42F,
              "NO WALLPAPERS - RUN VIBEWALL SCAN", 3.0F,
              0.9F, 0.9F, 0.9F, 1.0F);
    return;
  }

  switch (mode) {
  case DisplayMode::Slice:
    render_slice(wallpapers, selected);
    break;
  case DisplayMode::Grid:
    render_grid(wallpapers, selected);
    break;
  case DisplayMode::Hex:
    render_hex(wallpapers, selected);
    break;
  case DisplayMode::Mosaic:
    render_mosaic(wallpapers, selected);
    break;
  }
}

void Renderer::render_grid(const std::vector<Wallpaper> &wallpapers, int selected) {
  const float gap = 7.0F;
  const int cols = width_ >= 1500 ? 5 : (width_ >= 1100 ? 4 : 3);
  const int visible_rows = height_ >= 920 ? 4 : 3;
  const float cell_w = std::min(260.0F, (content_w_ - gap * (cols - 1)) / cols);
  const float cell_h = cell_w * 0.56F;
  const float grid_w = cols * cell_w + (cols - 1) * gap;
  const int selected_row = std::max(0, selected / cols);
  const int start_row = std::max(0, selected_row - visible_rows / 2);
  const int start = start_row * cols;
  const int end = std::min<int>(wallpapers.size(), start + visible_rows * cols);
  const float left = content_x_ + (content_w_ - grid_w) * 0.5F;
  const float total_h = visible_rows * cell_h + (visible_rows - 1) * gap;
  const float top = content_y_ + std::max(0.0F, (content_h_ - total_h) * 0.44F);

  for (int i = start; i < end; ++i) {
    const int local = i - start;
    const int row = local / cols;
    const int col = local % cols;
    const float x = left + col * (cell_w + gap);
    const float y = top + row * (cell_h + gap);
    const auto color = color_from_group(wallpapers[i].color_group);
    draw_rect(x - 3, y - 3, cell_w + 6, cell_h + 6, i == selected ? 0.52F : color[0],
              i == selected ? 0.96F : color[1], i == selected ? 0.78F : color[2],
              i == selected ? 0.98F : 0.28F);
    if (i == selected) {
      draw_rect(x - 7, y - 7, cell_w + 14, cell_h + 14, 0.52F, 0.96F, 0.78F, 0.16F);
    }
    draw_wallpaper_preview(x, y, cell_w, cell_h, wallpapers[i], 1.0F);
    if (i != selected) {
      draw_rect(x, y, cell_w, cell_h, 0.0F, 0.0F, 0.0F, 0.05F);
    }
    draw_wallpaper_badges(x, y, cell_w, cell_h, wallpapers[i], i == selected);
    if (i == selected) {
      draw_rect(x, y + cell_h - 28, cell_w, 28, 0.02F, 0.025F, 0.032F, 0.58F);
      draw_text(x + 48, y + cell_h - 19, short_name(wallpapers[i], 24), 1.45F, 0.94F, 0.96F,
                1.0F, 1.0F);
    }
    hit_regions_.push_back({i, {{x, y}, {x + cell_w, y}, {x + cell_w, y + cell_h}, {x, y + cell_h}}});
  }
}

void Renderer::render_slice(const std::vector<Wallpaper> &wallpapers, int selected) {
  const int count = static_cast<int>(wallpapers.size());
  if (count == 0) {
    return;
  }
  const float center_x = static_cast<float>(width_) * 0.5F;
  const float center_y = static_cast<float>(height_) * 0.69F;
  const float side_h = std::clamp(static_cast<float>(height_) * 0.39F, 260.0F, 410.0F);
  const float side_w = std::clamp(static_cast<float>(width_) * 0.055F, 72.0F, 122.0F);
  const float side_step = std::clamp(static_cast<float>(width_) * 0.048F, 58.0F, 94.0F);
  for (int offset = -7; offset <= 7; ++offset) {
    if (offset == 0) {
      continue;
    }
    const int index = (selected + offset + count) % count;
    const float w = side_w;
    const float h = side_h;
    const float x = center_x + offset * side_step - w * 0.5F;
    const float y = center_y - h * 0.5F + std::abs(offset) * 9.0F;
    const float skew = std::min(34.0F, w * 0.42F);
    const auto color = color_from_group(wallpapers[index].color_group);
    const std::vector<Point> poly = {{x + skew, y}, {x + w, y}, {x + w - skew, y + h}, {x, y + h}};
    draw_polygon(poly, color[0], color[1], color[2], 0.34F);
    std::vector<Point> inner;
    inner.reserve(poly.size());
    for (const auto &point : poly) {
      inner.push_back({x + w * 0.5F + (point.x - x - w * 0.5F) * 0.90F,
                       y + h * 0.5F + (point.y - y - h * 0.5F) * 0.94F});
    }
    draw_wallpaper_polygon(inner, wallpapers[index], 0.82F);
    draw_rect(x, y, w, h, 0.0F, 0.0F, 0.0F, 0.18F);
    draw_wallpaper_badges(x + 4.0F, y + 8.0F, w - 8.0F, h - 16.0F, wallpapers[index], false);
    hit_regions_.push_back({index, poly});
  }
  if (selected >= 0 && selected < count) {
    const float w = std::min(860.0F, content_w_ * 0.58F);
    const float h = std::min(500.0F, std::max(320.0F, w * 0.56F));
    const float x = center_x - w * 0.5F;
    const float y = center_y - h * 0.5F;
    draw_rect(x - 7, y - 7, w + 14, h + 14, 0.52F, 0.96F, 0.78F, 0.98F);
    draw_rect(x - 13, y - 13, w + 26, h + 26, 0.52F, 0.96F, 0.78F, 0.12F);
    draw_wallpaper_preview(x, y, w, h, wallpapers[selected], 1.0F);
    draw_wallpaper_badges(x, y, w, h, wallpapers[selected], true);
    draw_rect(x, y + h - 34, w, 34, 0.02F, 0.025F, 0.032F, 0.56F);
    draw_text(x + 16, y + h - 23, short_name(wallpapers[selected], 42), 1.65F, 0.96F, 0.97F, 1.0F,
              1.0F);
    draw_text(x + w - 62, y + h - 22, type_label(wallpapers[selected].type), 1.25F, 0.50F, 0.86F,
              0.74F, 1.0F);
    hit_regions_.push_back({selected, {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}});
  }
}

void Renderer::render_hex(const std::vector<Wallpaper> &wallpapers, int selected) {
  const float r = std::clamp(static_cast<float>(width_) * 0.052F, 70.0F, 108.0F);
  const float step_x = r * 1.55F;
  const float step_y = r * 1.34F;
  const int cols = std::min(7, std::max(3, static_cast<int>((content_w_ - step_x * 0.5F) / step_x)));
  const int selected_row = std::max(0, selected / cols);
  const int visible_rows = std::clamp(static_cast<int>((content_h_ + step_y * 0.5F) / step_y), 2, 4);
  const int start_row = std::max(0, selected_row - visible_rows / 2);
  const int start = start_row * cols;
  const int end = std::min<int>(wallpapers.size(), start + visible_rows * cols);
  const float hex_w = cols * step_x + step_x * 0.5F;
  const float left = content_x_ + (content_w_ - hex_w) * 0.5F + r;
  const float hex_h = visible_rows * step_y + r;
  const float top = content_y_ + std::max(0.0F, (content_h_ - hex_h) * 0.5F) + r;

  for (int i = start; i < end; ++i) {
    const int local = i - start;
    const int row = local / cols;
    const int col = local % cols;
    const float cx = left + col * step_x + (row % 2 == 0 ? 0.0F : step_x * 0.5F);
    const float cy = top + row * step_y;
    std::vector<Point> poly;
    for (int k = 0; k < 6; ++k) {
      const float angle = static_cast<float>(M_PI / 6.0 + k * M_PI / 3.0);
      poly.push_back({cx + std::cos(angle) * r, cy + std::sin(angle) * r});
    }
    const auto color = color_from_group(wallpapers[i].color_group);
    if (i == selected) {
      std::vector<Point> outline;
      outline.reserve(poly.size());
      for (const auto &point : poly) {
        outline.push_back({cx + (point.x - cx) * 1.08F, cy + (point.y - cy) * 1.08F});
      }
      draw_polygon(outline, 0.52F, 0.96F, 0.78F, 0.98F);
    }
    draw_polygon(poly, color[0], color[1], color[2], i == selected ? 0.36F : 0.22F);
    std::vector<Point> inner;
    inner.reserve(poly.size());
    for (const auto &point : poly) {
      inner.push_back({cx + (point.x - cx) * 0.94F, cy + (point.y - cy) * 0.94F});
    }
    draw_wallpaper_polygon(inner, wallpapers[i], 1.0F);
    if (i != selected) {
      draw_polygon(inner, 0.0F, 0.0F, 0.0F, 0.10F);
    }
    draw_tiny_badge(cx - r * 0.36F, cy + r * 0.46F, type_label(wallpapers[i].type), 0.52F, 0.96F,
                    0.78F);
    if (wallpapers[i].type == WallpaperType::Video) {
      draw_tiny_badge(cx + r * 0.24F, cy - r * 0.52F, "PLAY", 0.52F, 0.96F, 0.78F);
    }
    if (i == selected) {
      draw_rect(cx - r * 1.18F, cy + r * 0.76F, r * 2.36F, 24.0F, 0.02F, 0.025F, 0.032F, 0.58F);
      draw_text(cx - r * 1.05F, cy + r * 0.86F, short_name(wallpapers[i], 22), 1.35F, 0.96F,
                0.96F, 0.96F, 1.0F);
    }
    hit_regions_.push_back({i, poly});
  }

  const float search_y = static_cast<float>(height_) - 116.0F;
  draw_rect(content_x_, search_y, content_w_, 20.0F, 0.015F, 0.026F, 0.032F, 0.38F);
  draw_text(content_x_ + 14.0F, search_y + 6.0F, "/ SEARCH TAGS OR NAMES", 1.18F, 0.70F, 0.86F,
            0.95F, 1.0F);
  float tx = content_x_;
  float ty = search_y + 28.0F;
  for (ColorGroup group : color_groups()) {
    const auto color = color_from_group(group);
    const std::string label = color_label(group);
    const float pill_w = std::max(36.0F, static_cast<float>(label.size()) * 7.0F + 14.0F);
    if (tx + pill_w > content_x_ + content_w_) {
      tx = content_x_;
      ty += 22.0F;
    }
    draw_rect(tx, ty, pill_w, 17.0F, 0.025F, 0.055F, 0.060F, 0.62F);
    draw_rect(tx, ty, 3.0F, 17.0F, color[0], color[1], color[2], 0.95F);
    draw_text(tx + 8.0F, ty + 5.0F, label, 1.0F, 0.70F, 0.86F, 0.92F, 1.0F);
    tx += pill_w + 5.0F;
  }
}

void Renderer::render_mosaic(const std::vector<Wallpaper> &wallpapers, int selected) {
  const float gap = 7.0F;
  const int cols = width_ >= 1500 ? 5 : 4;
  const float base_w = std::min(260.0F, (content_w_ - gap * (cols - 1)) / cols);
  const float base_h = base_w * 0.54F;
  const int selected_row = std::max(0, selected / cols);
  const int start_row = std::max(0, selected_row - 2);
  const int start = start_row * cols;
  const int end = std::min<int>(wallpapers.size(), start + cols * 5);
  const float left = content_x_ + (content_w_ - (cols * base_w + (cols - 1) * gap)) * 0.5F;
  const float top = content_y_ + 8.0F;
  std::vector<float> col_y(static_cast<std::size_t>(cols), top);

  for (int i = start; i < end; ++i) {
    const int local = i - start;
    int col = local % cols;
    auto min_it = std::min_element(col_y.begin(), col_y.end());
    col = static_cast<int>(std::distance(col_y.begin(), min_it));

    float w = base_w;
    float h = base_h;
    const float aspect = wallpapers[i].height > 0
                             ? static_cast<float>(wallpapers[i].width) /
                                   static_cast<float>(wallpapers[i].height)
                             : 1.78F;
    if (aspect > 2.15F && col + 1 < cols && col_y[static_cast<std::size_t>(col + 1)] <= *min_it + 8.0F) {
      w = base_w * 2.0F + gap;
      h = base_h;
    } else if (aspect < 0.82F) {
      h = base_h * 1.72F + gap;
    }

    const float x = left + col * (base_w + gap);
    const float y = col_y[static_cast<std::size_t>(col)];
    if (y + h > content_y_ + content_h_) {
      continue;
    }
    draw_rect(x - 3, y - 3, w + 6, h + 6, i == selected ? 0.52F : 0.08F,
              i == selected ? 0.96F : 0.11F, i == selected ? 0.78F : 0.13F,
              i == selected ? 0.98F : 0.34F);
    draw_wallpaper_preview(x, y, w, h, wallpapers[i], 1.0F);
    if (i != selected) {
      draw_rect(x, y, w, h, 0.0F, 0.0F, 0.0F, 0.04F);
    }
    draw_wallpaper_badges(x, y, w, h, wallpapers[i], i == selected);
    hit_regions_.push_back({i, {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}});

    col_y[static_cast<std::size_t>(col)] += h + gap;
    if (w > base_w && col + 1 < cols) {
      col_y[static_cast<std::size_t>(col + 1)] = col_y[static_cast<std::size_t>(col)];
    }
  }
}

int Renderer::hit_test(float x, float y) const {
  for (const auto &region : hit_regions_) {
    if (region.polygon.size() >= 3 && point_in_polygon(x, y, region.polygon)) {
      return region.index;
    }
  }
  return -1;
}

PickerAction Renderer::action_hit_test(float x, float y) const {
  for (const auto &region : action_regions_) {
    if (x >= region.x && x <= region.x + region.w && y >= region.y && y <= region.y + region.h) {
      return region.action;
    }
  }
  return PickerAction::None;
}

bool Renderer::stage_contains(float x, float y) const {
  if (action_hit_test(x, y) != PickerAction::None || hit_test(x, y) >= 0) {
    return true;
  }
  return false;
}

} // namespace vibewall::picker
