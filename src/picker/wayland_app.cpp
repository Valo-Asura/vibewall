#include "picker/wayland_app.hpp"

#include "core/wallhaven_client.hpp"
#include "core/wallpaper_apply.hpp"
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#include <GLES2/gl2.h>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

namespace vibewall::picker {

namespace {
const wl_registry_listener registry_listener = {WaylandApp::registry_global,
                                                WaylandApp::registry_remove};
const zwlr_layer_surface_v1_listener layer_listener = {WaylandApp::layer_configure,
                                                       WaylandApp::layer_closed};
const wl_seat_listener seat_listener = {WaylandApp::seat_capabilities, WaylandApp::seat_name};
const wl_pointer_listener pointer_listener = {
    .enter = WaylandApp::pointer_enter,
    .leave = WaylandApp::pointer_leave,
    .motion = WaylandApp::pointer_motion,
    .button = WaylandApp::pointer_button,
    .axis = WaylandApp::pointer_axis,
    .frame = WaylandApp::pointer_frame,
    .axis_source = WaylandApp::pointer_axis_source,
    .axis_stop = WaylandApp::pointer_axis_stop,
    .axis_discrete = WaylandApp::pointer_axis_discrete,
    .axis_value120 = WaylandApp::pointer_axis_value120,
    .axis_relative_direction = WaylandApp::pointer_axis_relative_direction,
};
const wl_keyboard_listener keyboard_listener = {
    WaylandApp::keyboard_keymap, WaylandApp::keyboard_enter,     WaylandApp::keyboard_leave,
    WaylandApp::keyboard_key,    WaylandApp::keyboard_modifiers, WaylandApp::keyboard_repeat,
};

EGLConfig choose_config(EGLDisplay display) {
  const EGLint attrs[] = {EGL_SURFACE_TYPE,
                          EGL_WINDOW_BIT,
                          EGL_RED_SIZE,
                          8,
                          EGL_GREEN_SIZE,
                          8,
                          EGL_BLUE_SIZE,
                          8,
                          EGL_ALPHA_SIZE,
                          8,
                          EGL_RENDERABLE_TYPE,
                          EGL_OPENGL_ES2_BIT,
                          EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (!eglChooseConfig(display, attrs, &config, 1, &count) || count == 0) {
    throw std::runtime_error("eglChooseConfig failed");
  }
  return config;
}
} // namespace

WaylandApp::WaylandApp(AppConfig config, Database &db, DisplayMode mode, bool benchmark_ready,
                       bool start_wallhaven)
    : config_(std::move(config)), db_(db), mode_(mode), benchmark_ready_(benchmark_ready),
      start_wallhaven_(start_wallhaven) {}

WaylandApp::~WaylandApp() {
  cleanup();
}

void WaylandApp::connect() {
  display_ = wl_display_connect(nullptr);
  if (display_ == nullptr) {
    throw std::runtime_error("failed to connect to Wayland display");
  }
  registry_ = wl_display_get_registry(display_);
  wl_registry_add_listener(registry_, &registry_listener, this);
  wl_display_roundtrip(display_);
  if (compositor_ == nullptr || layer_shell_ == nullptr) {
    throw std::runtime_error("Wayland compositor or wlr-layer-shell is unavailable");
  }

  surface_ = wl_compositor_create_surface(compositor_);
  layer_surface_ = zwlr_layer_shell_v1_get_layer_surface(
      layer_shell_, surface_, nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "vibewallREzero");
  zwlr_layer_surface_v1_set_anchor(layer_surface_, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                       ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
  zwlr_layer_surface_v1_set_exclusive_zone(layer_surface_, -1);
  zwlr_layer_surface_v1_set_keyboard_interactivity(
      layer_surface_, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
  zwlr_layer_surface_v1_set_size(layer_surface_, 0, 0);
  zwlr_layer_surface_v1_add_listener(layer_surface_, &layer_listener, this);
  wl_surface_commit(surface_);
  while (!configured_ && wl_display_dispatch(display_) != -1) {
  }
}

void WaylandApp::setup_egl() {
  egl_display_ = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(display_));
  if (egl_display_ == EGL_NO_DISPLAY) {
    throw std::runtime_error("eglGetDisplay failed");
  }
  if (!eglInitialize(egl_display_, nullptr, nullptr)) {
    throw std::runtime_error("eglInitialize failed");
  }
  const EGLConfig config = choose_config(egl_display_);
  const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  egl_context_ = eglCreateContext(egl_display_, config, EGL_NO_CONTEXT, context_attrs);
  if (egl_context_ == EGL_NO_CONTEXT) {
    throw std::runtime_error("eglCreateContext failed");
  }
  egl_window_ = wl_egl_window_create(surface_, width_, height_);
  if (egl_window_ == nullptr) {
    throw std::runtime_error("wl_egl_window_create failed");
  }
  egl_surface_ = eglCreateWindowSurface(egl_display_, config,
                                        reinterpret_cast<EGLNativeWindowType>(egl_window_), nullptr);
  if (egl_surface_ == EGL_NO_SURFACE) {
    throw std::runtime_error("eglCreateWindowSurface failed");
  }
  if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
    throw std::runtime_error("eglMakeCurrent failed");
  }
  renderer_.init();
  renderer_.resize(width_, height_);
}

void WaylandApp::load_wallpapers() {
  if (wallhaven_mode_) {
    wallpapers_.clear();
    int count = 0;
    for (const auto &entry : db_.cached_wallhaven()) {
      if (entry.thumb_path.empty() || !std::filesystem::exists(entry.thumb_path)) {
        continue;
      }
      Wallpaper item;
      item.path = entry.image_url;
      item.name = "wallhaven-" + entry.id;
      item.type = WallpaperType::Wallhaven;
      item.thumb_path = entry.thumb_path;
      item.width = entry.width;
      item.height = entry.height;
      item.added_at = entry.cached_at;
      wallpapers_.push_back(std::move(item));
      if (++count >= 120) {
        break;
      }
    }
    if (selected_ >= static_cast<int>(wallpapers_.size())) {
      selected_ = std::max(0, static_cast<int>(wallpapers_.size()) - 1);
    }
    return;
  }
  Filter filter;
  filter.query = query_;
  filter.type = type_filter_;
  filter.color_group = color_filter_;
  filter.favorites_only = favorites_only_;
  wallpapers_ = db_.list_wallpapers(filter);
  if (selected_ >= static_cast<int>(wallpapers_.size())) {
    selected_ = std::max(0, static_cast<int>(wallpapers_.size()) - 1);
  }
}

void WaylandApp::refresh_background_path() {
  const std::string last =
      db_.setting("last.applied_path").value_or(db_.setting("last.path").value_or(""));
  if (last.empty()) {
    background_path_.clear();
    return;
  }
  if (const auto existing = db_.find_by_path(last); existing.has_value()) {
    background_path_ = existing->type == WallpaperType::Video && !existing->thumb_path.empty()
                           ? existing->thumb_path
                           : existing->path;
    return;
  }
  background_path_ = last;
}

void WaylandApp::mark_data_dirty() {
  data_dirty_ = true;
}

void WaylandApp::redraw() {
  if (data_dirty_) {
    refresh_background_path();
    load_wallpapers();
    data_dirty_ = false;
  }
  renderer_.render(wallpapers_, selected_, scroll_offset_, mode_, query_, wallhaven_mode_, status_, type_filter_,
                   color_filter_, favorites_only_, background_path_,
                   wallhaven_sort_, wallhaven_sfw_);
  eglSwapBuffers(egl_display_, egl_surface_);
}

void WaylandApp::apply_selected() {
  if (wallpapers_.empty() || selected_ < 0 || selected_ >= static_cast<int>(wallpapers_.size())) {
    return;
  }
  if (wallpapers_[selected_].type == WallpaperType::Wallhaven) {
    download_selected_wallhaven(true);
    return;
  }
  const ApplyResult result = apply_wallpaper(db_, config_, wallpapers_[selected_]);
  if (!result.ok) {
    std::cerr << "apply failed: " << result.message << '\n';
    status_ = "APPLY FAILED";
    redraw();
  } else if (config_.close_on_selection) {
    running_ = false;
  } else {
    status_ = "APPLIED";
    mark_data_dirty();
    redraw();
  }
}

std::optional<WallhavenEntry> WaylandApp::selected_wallhaven_entry() const {
  if (wallpapers_.empty() || selected_ < 0 || selected_ >= static_cast<int>(wallpapers_.size()) ||
      wallpapers_[selected_].type != WallpaperType::Wallhaven) {
    return std::nullopt;
  }
  for (const auto &entry : db_.cached_wallhaven()) {
    if (entry.image_url == wallpapers_[selected_].path ||
        "wallhaven-" + entry.id == wallpapers_[selected_].name) {
      return entry;
    }
  }
  return std::nullopt;
}

void WaylandApp::download_selected_wallhaven(bool apply_after_download) {
  const auto match = selected_wallhaven_entry();
  if (!match.has_value()) {
    status_ = "SELECT WEB WALLPAPER";
    redraw();
    return;
  }
  try {
    const auto downloaded = wallhaven_download(config_, *match);
    if (!apply_after_download) {
      status_ = "DOWNLOADED " + downloaded.filename().string().substr(0, 24);
      redraw();
      return;
    }
    const ApplyResult result = apply_path(db_, config_, downloaded.string(), WallpaperType::Image);
    if (!result.ok) {
      status_ = "APPLY FAILED";
      std::cerr << "apply failed: " << result.message << '\n';
      redraw();
      return;
    }
    if (config_.close_on_selection) {
      running_ = false;
    } else {
      status_ = "DOWNLOADED + APPLIED";
      redraw();
    }
  } catch (const std::exception &err) {
    status_ = apply_after_download ? "WEB APPLY FAILED" : "DOWNLOAD FAILED";
    std::cerr << "wallhaven download failed: " << err.what() << '\n';
    redraw();
  }
}

void WaylandApp::toggle_favorite() {
  toggle_favorite_at(selected_);
}

void WaylandApp::toggle_favorite_at(int index) {
  if (wallpapers_.empty()) {
    return;
  }
  if (index < 0 || index >= static_cast<int>(wallpapers_.size())) {
    return;
  }
  selected_ = index;
  if (wallpapers_[selected_].type == WallpaperType::Wallhaven) {
    status_ = "DOWNLOAD WEB FIRST";
    redraw();
    return;
  }
  const bool next = !wallpapers_[selected_].favorite;
  db_.set_favorite(wallpapers_[selected_].path, next);
  wallpapers_[selected_].favorite = next;
  status_ = next ? "FAVORITE SAVED" : "FAVORITE REMOVED";
  if (favorites_only_) {
    mark_data_dirty();
  }
  redraw();
}

void WaylandApp::load_wallhaven() {
  if (!config_.enable_wallhaven) {
    status_ = "WALLHAVEN DISABLED";
    redraw();
    return;
  }
  if (query_.empty() && !db_.cached_wallhaven().empty()) {
    wallhaven_mode_ = true;
    type_filter_.reset();
    color_filter_.reset();
    mode_ = DisplayMode::Grid;
    selected_ = 0;
    scroll_offset_ = 0;
    status_ = "WALLHAVEN CACHE";
    mark_data_dirty();
    redraw();
    return;
  }
  start_wallhaven_load(1, true, "LOADING WEB...");
}

void WaylandApp::load_wallhaven_next_page() {
  if (!config_.enable_wallhaven || !wallhaven_mode_) {
    return;
  }
  start_wallhaven_load(wallhaven_page_ + 1, false,
                       "LOADING PAGE " + std::to_string(wallhaven_page_ + 1) + "...");
}

void WaylandApp::set_wallhaven_sort(const std::string &sort) {
  wallhaven_sort_ = sort;
  wallhaven_page_ = 1;
  start_wallhaven_load(1, true, "LOADING " + sort + "...");
}

void WaylandApp::start_wallhaven_load(int page, bool reset_view, std::string loading_status) {
  if (!config_.enable_wallhaven) {
    status_ = "WALLHAVEN DISABLED";
    redraw();
    return;
  }
  if (wallhaven_loading_.load(std::memory_order_acquire)) {
    status_ = "WEB STILL LOADING";
    redraw();
    return;
  }
  if (wallhaven_worker_.joinable()) {
    wallhaven_worker_.join();
  }

  wallhaven_mode_ = true;
  type_filter_.reset();
  color_filter_.reset();
  mode_ = DisplayMode::Grid;
  if (reset_view) {
    selected_ = 0;
    scroll_offset_ = 0;
  }

  const std::string search = query_.empty() ? config_.wallhaven_default_query : query_;
  const std::string purity = wallhaven_sfw_ ? "100" : "110";
  const AppConfig config = config_;
  const std::string sort = wallhaven_sort_;
  const int target_page = std::max(1, page);

  {
    std::lock_guard lock(wallhaven_mutex_);
    wallhaven_pending_success_ = false;
    wallhaven_pending_page_ = target_page;
    wallhaven_pending_status_.clear();
  }
  wallhaven_done_.store(false, std::memory_order_release);
  wallhaven_loading_.store(true, std::memory_order_release);
  status_ = std::move(loading_status);
  mark_data_dirty();
  redraw();

  wallhaven_worker_ = std::thread([this, config, search, target_page, sort, purity]() {
    bool success = false;
    int finished_page = target_page;
    std::string message;
    try {
      Database worker_db(config.db_path);
      worker_db.migrate();
      const int before = static_cast<int>(worker_db.cached_wallhaven().size());
      cache_wallhaven_search(worker_db, config, search, target_page, sort, purity);
      const int after = static_cast<int>(worker_db.cached_wallhaven().size());
      success = true;
      if (target_page > 1 && after <= before) {
        finished_page = std::max(1, target_page - 1);
        message = "NO MORE RESULTS";
      } else {
        message = "WEB PAGE " + std::to_string(target_page) + " READY";
      }
    } catch (const std::exception &err) {
      message = "WALLHAVEN ERROR";
      std::cerr << "wallhaven worker failed: " << err.what() << '\n';
    }

    {
      std::lock_guard lock(wallhaven_mutex_);
      wallhaven_pending_success_ = success;
      wallhaven_pending_page_ = finished_page;
      wallhaven_pending_status_ = std::move(message);
      wallhaven_loading_.store(false, std::memory_order_release);
      wallhaven_done_.store(true, std::memory_order_release);
    }
  });
}

void WaylandApp::finish_wallhaven_load_if_ready() {
  if (!wallhaven_done_.load(std::memory_order_acquire)) {
    return;
  }
  if (wallhaven_worker_.joinable()) {
    wallhaven_worker_.join();
  }

  bool success = false;
  int page = wallhaven_page_;
  std::string status;
  {
    std::lock_guard lock(wallhaven_mutex_);
    success = wallhaven_pending_success_;
    page = wallhaven_pending_page_;
    status = wallhaven_pending_status_;
  }

  if (success) {
    wallhaven_page_ = page;
  }
  status_ = status.empty() ? (success ? "WALLHAVEN READY" : "WALLHAVEN ERROR") : status;
  wallhaven_done_.store(false, std::memory_order_release);
  mark_data_dirty();
  redraw();
}

void WaylandApp::show_local() {
  wallhaven_mode_ = false;
  if (type_filter_ == WallpaperType::Wallhaven) {
    type_filter_.reset();
  }
  status_ = "LOCAL WALLPAPERS";
  selected_ = 0;
  scroll_offset_ = 0;
  mark_data_dirty();
  redraw();
}

void WaylandApp::apply_random() {
  const auto result = apply_random_wallpaper(db_, config_);
  if (!result.ok) {
    status_ = "RANDOM FAILED";
    std::cerr << "random failed: " << result.message << '\n';
    redraw();
  } else if (config_.close_on_selection) {
    running_ = false;
  } else {
    status_ = "RANDOM APPLIED";
    mark_data_dirty();
    redraw();
  }
}

void WaylandApp::move_selection(int delta) {
  if (wallpapers_.empty()) {
    return;
  }
  selected_ += delta;
  if (selected_ < 0) {
    selected_ = 0;
  }
  if (selected_ >= static_cast<int>(wallpapers_.size())) {
    selected_ = static_cast<int>(wallpapers_.size()) - 1;
    if (wallhaven_mode_ && delta > 0) {
      load_wallhaven_next_page();
      return;
    }
  }

  // Adjust scroll_offset_ to make sure selected_ is in the viewport
  const int cols = scroll_step();
  if (cols > 0) {
    const int selected_row = selected_ / cols;
    const int start_row = scroll_offset_ / cols;
    const int visible = visible_rows_count();
    if (selected_row < start_row) {
      scroll_offset_ = selected_row * cols;
    } else if (selected_row >= start_row + visible) {
      scroll_offset_ = (selected_row - visible + 1) * cols;
    }
  }
  redraw();
}

void WaylandApp::scroll_viewport(int delta) {
  if (wallpapers_.empty()) {
    return;
  }
  const int cols = scroll_step();
  if (cols <= 0) {
    return;
  }
  scroll_offset_ += delta;
  // Align to rows
  scroll_offset_ = (scroll_offset_ / cols) * cols;
  if (scroll_offset_ < 0) {
    scroll_offset_ = 0;
  }
  const int max_scroll = std::max(0, (static_cast<int>(wallpapers_.size()) - 1) / cols * cols);
  if (scroll_offset_ > max_scroll) {
    scroll_offset_ = max_scroll;
    if (wallhaven_mode_ && delta > 0) {
      load_wallhaven_next_page();
      return;
    }
  }
  redraw();
}

int WaylandApp::visible_rows_count() const {
  switch (mode_) {
  case DisplayMode::Grid:
    return height_ >= 920 ? 4 : 3;
  case DisplayMode::Mosaic:
    return 5;
  case DisplayMode::Hex: {
    const float r = std::clamp(static_cast<float>(width_) * 0.052F, 70.0F, 108.0F);
    const float step_y = r * 1.34F;
    const float content_h = static_cast<float>(height_) - 180.0F;
    return std::clamp(static_cast<int>((content_h + step_y * 0.5F) / step_y), 2, 4);
  }
  case DisplayMode::Slice:
    return 1;
  }
  return 1;
}

int WaylandApp::scroll_step() const {
  switch (mode_) {
  case DisplayMode::Grid:
    return width_ >= 1500 ? 5 : (width_ >= 1100 ? 4 : 3);
  case DisplayMode::Mosaic:
    return width_ >= 1500 ? 5 : 4;
  case DisplayMode::Hex:
    return width_ >= 1500 ? 7 : (width_ >= 1100 ? 5 : 3);
  case DisplayMode::Slice:
    return 1;
  }
  return 1;
}

void WaylandApp::set_type_filter(std::optional<WallpaperType> type) {
  if (type == WallpaperType::Wallhaven) {
    load_wallhaven();
    return;
  }
  wallhaven_mode_ = false;
  type_filter_ = type;
  selected_ = 0;
  scroll_offset_ = 0;
  mark_data_dirty();
  redraw();
}

void WaylandApp::set_color_filter(std::optional<ColorGroup> color) {
  wallhaven_mode_ = false;
  if (type_filter_ == WallpaperType::Wallhaven) {
    type_filter_.reset();
  }
  color_filter_ = color;
  selected_ = 0;
  scroll_offset_ = 0;
  mark_data_dirty();
  redraw();
}

void WaylandApp::set_query(std::string query) {
  query_ = std::move(query);
  selected_ = 0;
  scroll_offset_ = 0;
  mark_data_dirty();
  redraw();
}

void WaylandApp::mark_benchmark_ready() const {
  if (!benchmark_ready_) {
    return;
  }
  const auto path = "/tmp/vibewall-picker-ready-" + std::to_string(getpid());
  std::ofstream out(path);
  out << "ready\n";
}

int WaylandApp::run() {
  connect();
  setup_egl();
  if (start_wallhaven_) {
    load_wallhaven();
  } else {
    redraw();
  }
  mark_benchmark_ready();
  const int fd = wl_display_get_fd(display_);
  while (running_) {
    finish_wallhaven_load_if_ready();
    while (wl_display_prepare_read(display_) != 0) {
      if (wl_display_dispatch_pending(display_) == -1) {
        running_ = false;
        break;
      }
      finish_wallhaven_load_if_ready();
    }
    if (!running_) {
      break;
    }

    wl_display_flush(display_);
    pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
    const int timeout_ms = wallhaven_loading_.load(std::memory_order_acquire) ? 120 : 250;
    const int rc = poll(&pfd, 1, timeout_ms);
    if (rc > 0 && (pfd.revents & POLLIN) != 0) {
      wl_display_read_events(display_);
      if (wl_display_dispatch_pending(display_) == -1) {
        running_ = false;
      }
    } else {
      wl_display_cancel_read(display_);
    }
  }
  finish_wallhaven_load_if_ready();
  return 0;
}

void WaylandApp::cleanup() {
  running_ = false;
  if (wallhaven_worker_.joinable()) {
    wallhaven_worker_.join();
  }
  renderer_.shutdown();
  if (egl_display_ != EGL_NO_DISPLAY) {
    eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (egl_surface_ != EGL_NO_SURFACE) {
      eglDestroySurface(egl_display_, egl_surface_);
    }
    if (egl_context_ != EGL_NO_CONTEXT) {
      eglDestroyContext(egl_display_, egl_context_);
    }
    eglTerminate(egl_display_);
  }
  if (egl_window_ != nullptr) {
    wl_egl_window_destroy(egl_window_);
  }
  if (xkb_state_ != nullptr) {
    xkb_state_unref(xkb_state_);
  }
  if (xkb_keymap_ != nullptr) {
    xkb_keymap_unref(xkb_keymap_);
  }
  if (xkb_context_ != nullptr) {
    xkb_context_unref(xkb_context_);
  }
  if (keyboard_ != nullptr) {
    wl_keyboard_destroy(keyboard_);
  }
  if (pointer_ != nullptr) {
    wl_pointer_destroy(pointer_);
  }
  if (seat_ != nullptr) {
    wl_seat_destroy(seat_);
  }
  if (layer_surface_ != nullptr) {
    zwlr_layer_surface_v1_destroy(layer_surface_);
  }
  if (surface_ != nullptr) {
    wl_surface_destroy(surface_);
  }
  if (layer_shell_ != nullptr) {
    zwlr_layer_shell_v1_destroy(layer_shell_);
  }
  if (compositor_ != nullptr) {
    wl_compositor_destroy(compositor_);
  }
  if (registry_ != nullptr) {
    wl_registry_destroy(registry_);
  }
  if (display_ != nullptr) {
    wl_display_disconnect(display_);
  }
  egl_display_ = EGL_NO_DISPLAY;
  egl_surface_ = EGL_NO_SURFACE;
  egl_context_ = EGL_NO_CONTEXT;
  egl_window_ = nullptr;
  display_ = nullptr;
}

void WaylandApp::registry_global(void *data, wl_registry *registry, uint32_t name,
                                 const char *interface, uint32_t version) {
  auto *app = static_cast<WaylandApp *>(data);
  if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
    app->compositor_ = static_cast<wl_compositor *>(
        wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4U)));
  } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    app->layer_shell_ = static_cast<zwlr_layer_shell_v1 *>(
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4));
  } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
    app->seat_ =
        static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 7));
    wl_seat_add_listener(app->seat_, &seat_listener, app);
  }
}

void WaylandApp::registry_remove(void *, wl_registry *, uint32_t) {}

void WaylandApp::layer_configure(void *data, zwlr_layer_surface_v1 *surface, uint32_t serial,
                                 uint32_t width, uint32_t height) {
  auto *app = static_cast<WaylandApp *>(data);
  zwlr_layer_surface_v1_ack_configure(surface, serial);
  app->width_ = width == 0 ? 1280 : static_cast<int>(width);
  app->height_ = height == 0 ? 760 : static_cast<int>(height);
  if (app->egl_window_ != nullptr) {
    wl_egl_window_resize(app->egl_window_, app->width_, app->height_, 0, 0);
    app->renderer_.resize(app->width_, app->height_);
  }
  app->configured_ = true;
}

void WaylandApp::layer_closed(void *data, zwlr_layer_surface_v1 *) {
  static_cast<WaylandApp *>(data)->running_ = false;
}

void WaylandApp::seat_capabilities(void *data, wl_seat *seat, uint32_t capabilities) {
  auto *app = static_cast<WaylandApp *>(data);
  if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && app->pointer_ == nullptr) {
    app->pointer_ = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(app->pointer_, &pointer_listener, app);
  }
  if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && app->keyboard_ == nullptr) {
    app->keyboard_ = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(app->keyboard_, &keyboard_listener, app);
  }
}

void WaylandApp::seat_name(void *, wl_seat *, const char *) {}
void WaylandApp::pointer_enter(void *data, wl_pointer *, uint32_t, wl_surface *, wl_fixed_t sx,
                               wl_fixed_t sy) {
  auto *app = static_cast<WaylandApp *>(data);
  app->pointer_x_ = wl_fixed_to_double(sx);
  app->pointer_y_ = wl_fixed_to_double(sy);
}
void WaylandApp::pointer_leave(void *, wl_pointer *, uint32_t, wl_surface *) {}
void WaylandApp::pointer_motion(void *data, wl_pointer *, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
  auto *app = static_cast<WaylandApp *>(data);
  app->pointer_x_ = wl_fixed_to_double(sx);
  app->pointer_y_ = wl_fixed_to_double(sy);
}
void WaylandApp::pointer_button(void *data, wl_pointer *, uint32_t, uint32_t, uint32_t button,
                                uint32_t state) {
  auto *app = static_cast<WaylandApp *>(data);
  if (button == 0x110 && state == WL_POINTER_BUTTON_STATE_PRESSED) {
    switch (app->renderer_.action_hit_test(app->pointer_x_, app->pointer_y_)) {
    case PickerAction::Slice:
      app->mode_ = DisplayMode::Slice;
      app->redraw();
      return;
    case PickerAction::Grid:
      app->mode_ = DisplayMode::Grid;
      app->redraw();
      return;
    case PickerAction::Hex:
      app->mode_ = DisplayMode::Hex;
      app->redraw();
      return;
    case PickerAction::Mosaic:
      app->mode_ = DisplayMode::Mosaic;
      app->redraw();
      return;
    case PickerAction::Wallhaven:
      app->load_wallhaven();
      return;
    case PickerAction::Local:
      app->show_local();
      return;
    case PickerAction::Random:
      app->apply_random();
      return;
    case PickerAction::Search:
      app->search_active_ = true;
      app->status_ = "TYPE SEARCH";
      app->redraw();
      return;
    case PickerAction::Download:
      app->download_selected_wallhaven(false);
      return;
    case PickerAction::Apply:
      app->apply_selected();
      return;
    case PickerAction::FavoriteFilter:
      app->favorites_only_ = !app->favorites_only_;
      app->selected_ = 0;
      app->scroll_offset_ = 0;
      app->mark_data_dirty();
      app->redraw();
      return;
    case PickerAction::TypeAll:
      app->set_type_filter(std::nullopt);
      return;
    case PickerAction::TypePic:
      app->set_type_filter(WallpaperType::Image);
      return;
    case PickerAction::TypeVid:
      app->set_type_filter(WallpaperType::Video);
      return;
    case PickerAction::TypeWeb:
      app->set_type_filter(WallpaperType::Wallhaven);
      return;
    case PickerAction::ColorAll:
      app->set_color_filter(std::nullopt);
      return;
    case PickerAction::ColorRed:
      app->set_color_filter(ColorGroup::Red);
      return;
    case PickerAction::ColorOrange:
      app->set_color_filter(ColorGroup::Orange);
      return;
    case PickerAction::ColorYellow:
      app->set_color_filter(ColorGroup::Yellow);
      return;
    case PickerAction::ColorLime:
      app->set_color_filter(ColorGroup::Lime);
      return;
    case PickerAction::ColorGreen:
      app->set_color_filter(ColorGroup::Green);
      return;
    case PickerAction::ColorCyan:
      app->set_color_filter(ColorGroup::Cyan);
      return;
    case PickerAction::ColorBlue:
      app->set_color_filter(ColorGroup::Blue);
      return;
    case PickerAction::ColorPurple:
      app->set_color_filter(ColorGroup::Purple);
      return;
    case PickerAction::ColorPink:
      app->set_color_filter(ColorGroup::Pink);
      return;
    case PickerAction::ColorBrown:
      app->set_color_filter(ColorGroup::Brown);
      return;
    case PickerAction::ColorWhite:
      app->set_color_filter(ColorGroup::White);
      return;
    case PickerAction::ColorGray:
      app->set_color_filter(ColorGroup::Gray);
      return;
    case PickerAction::ColorBlack:
      app->set_color_filter(ColorGroup::Black);
      return;
    case PickerAction::WallhavenTrend:
      app->set_wallhaven_sort("toplist");
      return;
    case PickerAction::WallhavenNew:
      app->set_wallhaven_sort("date_added");
      return;
    case PickerAction::WallhavenTop:
      app->set_wallhaven_sort("views");
      return;
    case PickerAction::WallhavenPopular:
      app->set_wallhaven_sort("hot");
      return;
    case PickerAction::WallhavenSFW:
      app->wallhaven_sfw_ = !app->wallhaven_sfw_;
      app->wallhaven_page_ = 1;
      app->start_wallhaven_load(1, true,
                                app->wallhaven_sfw_ ? "LOADING SFW..." : "LOADING NSFW...");
      return;
    case PickerAction::WallhavenNextPage:
      app->load_wallhaven_next_page();
      return;
    case PickerAction::None:
      break;
    }
    const int favorite_hit = app->renderer_.favorite_hit_test(app->pointer_x_, app->pointer_y_);
    if (favorite_hit >= 0) {
      app->toggle_favorite_at(favorite_hit);
      return;
    }
    const int hit = app->renderer_.hit_test(app->pointer_x_, app->pointer_y_);
    if (hit >= 0) {
      app->selected_ = hit;
      if (app->wallpapers_[hit].type == WallpaperType::Wallhaven) {
        app->status_ = "WEB SELECTED - ENTER APPLY - D DOWNLOAD";
        app->redraw();
      } else {
        app->apply_selected();
      }
    } else if (!app->renderer_.stage_contains(app->pointer_x_, app->pointer_y_)) {
      app->running_ = false;
    }
  }
}
void WaylandApp::pointer_axis(void *data, wl_pointer *, uint32_t, uint32_t axis, wl_fixed_t value) {
  auto *app = static_cast<WaylandApp *>(data);
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL && axis != WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
    return;
  }
  const double amount = wl_fixed_to_double(value);
  if (std::abs(amount) < 0.01) {
    return;
  }
  constexpr double tick = 15.0;
  app->scroll_accumulator_ += amount;
  int steps = 0;
  while (app->scroll_accumulator_ >= tick) {
    ++steps;
    app->scroll_accumulator_ -= tick;
  }
  while (app->scroll_accumulator_ <= -tick) {
    --steps;
    app->scroll_accumulator_ += tick;
  }
  if (steps != 0) {
    app->scroll_viewport(steps * app->scroll_step());
  }
}
void WaylandApp::pointer_frame(void *, wl_pointer *) {}
void WaylandApp::pointer_axis_source(void *, wl_pointer *, uint32_t) {}
void WaylandApp::pointer_axis_stop(void *, wl_pointer *, uint32_t, uint32_t) {}
void WaylandApp::pointer_axis_discrete(void *, wl_pointer *, uint32_t, int32_t) {}
void WaylandApp::pointer_axis_value120(void *, wl_pointer *, uint32_t, int32_t) {}
void WaylandApp::pointer_axis_relative_direction(void *, wl_pointer *, uint32_t, uint32_t) {}

void WaylandApp::keyboard_keymap(void *data, wl_keyboard *, uint32_t format, int fd,
                                 uint32_t size) {
  auto *app = static_cast<WaylandApp *>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  char *map = static_cast<char *>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
  if (map == MAP_FAILED) {
    close(fd);
    return;
  }
  if (app->xkb_state_ != nullptr) {
    xkb_state_unref(app->xkb_state_);
    app->xkb_state_ = nullptr;
  }
  if (app->xkb_keymap_ != nullptr) {
    xkb_keymap_unref(app->xkb_keymap_);
    app->xkb_keymap_ = nullptr;
  }
  if (app->xkb_context_ != nullptr) {
    xkb_context_unref(app->xkb_context_);
    app->xkb_context_ = nullptr;
  }
  app->xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (app->xkb_context_ == nullptr) {
    munmap(map, size);
    close(fd);
    return;
  }
  app->xkb_keymap_ = xkb_keymap_new_from_string(app->xkb_context_, map,
                                                XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (app->xkb_keymap_ == nullptr) {
    xkb_context_unref(app->xkb_context_);
    app->xkb_context_ = nullptr;
    munmap(map, size);
    close(fd);
    return;
  }
  app->xkb_state_ = xkb_state_new(app->xkb_keymap_);
  if (app->xkb_state_ == nullptr) {
    xkb_keymap_unref(app->xkb_keymap_);
    app->xkb_keymap_ = nullptr;
    xkb_context_unref(app->xkb_context_);
    app->xkb_context_ = nullptr;
  }
  munmap(map, size);
  close(fd);
}

void WaylandApp::keyboard_enter(void *, wl_keyboard *, uint32_t, wl_surface *, wl_array *) {}
void WaylandApp::keyboard_leave(void *, wl_keyboard *, uint32_t, wl_surface *) {}

void WaylandApp::keyboard_key(void *data, wl_keyboard *, uint32_t, uint32_t, uint32_t key,
                              uint32_t state) {
  auto *app = static_cast<WaylandApp *>(data);
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED || app->xkb_state_ == nullptr) {
    return;
  }
  const xkb_keycode_t code = key + 8;
  const xkb_keysym_t sym = xkb_state_key_get_one_sym(app->xkb_state_, code);

  if (sym == XKB_KEY_Escape) {
    if (app->search_active_) {
      app->search_active_ = false;
      app->set_query("");
    } else {
      app->running_ = false;
    }
    return;
  }
  if (app->search_active_) {
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
      app->search_active_ = false;
      if (app->wallhaven_mode_) {
        app->load_wallhaven();
      } else {
        app->redraw();
      }
      return;
    }
    if (sym == XKB_KEY_BackSpace) {
      if (!app->query_.empty()) {
        app->query_.pop_back();
        app->set_query(app->query_);
      }
      return;
    }
    char text[8] = {};
    const int n = xkb_state_key_get_utf8(app->xkb_state_, code, text, sizeof(text));
    const unsigned char first = static_cast<unsigned char>(text[0]);
    if (n > 0 && first >= 32) {
      app->query_.append(text, static_cast<std::size_t>(n));
      app->set_query(app->query_);
    }
    return;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
    app->apply_selected();
    return;
  }
  if (sym == XKB_KEY_Left) {
    app->move_selection(-1);
    return;
  }
  if (sym == XKB_KEY_Right) {
    app->move_selection(1);
    return;
  }
  if (sym == XKB_KEY_Up) {
    app->move_selection(-app->scroll_step());
    return;
  }
  if (sym == XKB_KEY_Down) {
    app->move_selection(app->scroll_step());
    return;
  }
  if (sym == XKB_KEY_1) {
    app->mode_ = DisplayMode::Slice;
    app->redraw();
    return;
  }
  if (sym == XKB_KEY_2) {
    app->mode_ = DisplayMode::Grid;
    app->redraw();
    return;
  }
  if (sym == XKB_KEY_3) {
    app->mode_ = DisplayMode::Hex;
    app->redraw();
    return;
  }
  if (sym == XKB_KEY_4) {
    app->mode_ = DisplayMode::Mosaic;
    app->redraw();
    return;
  }
  if (sym == XKB_KEY_w || sym == XKB_KEY_W) {
    app->load_wallhaven();
    return;
  }
  if (sym == XKB_KEY_l || sym == XKB_KEY_L) {
    app->show_local();
    return;
  }
  if (sym == XKB_KEY_r || sym == XKB_KEY_R) {
    app->apply_random();
    return;
  }
  if (sym == XKB_KEY_d || sym == XKB_KEY_D) {
    app->download_selected_wallhaven(false);
    return;
  }
  if (sym == XKB_KEY_f || sym == XKB_KEY_F) {
    app->toggle_favorite();
    return;
  }
  if (sym == XKB_KEY_slash) {
    app->search_active_ = true;
    return;
  }
}

void WaylandApp::keyboard_modifiers(void *data, wl_keyboard *, uint32_t, uint32_t depressed,
                                    uint32_t latched, uint32_t locked, uint32_t group) {
  auto *app = static_cast<WaylandApp *>(data);
  if (app->xkb_state_ != nullptr) {
    xkb_state_update_mask(app->xkb_state_, depressed, latched, locked, 0, 0, group);
  }
}

void WaylandApp::keyboard_repeat(void *, wl_keyboard *, int32_t, int32_t) {}

} // namespace vibewall::picker
