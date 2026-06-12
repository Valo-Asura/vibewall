#include "picker/wayland_app.hpp"

#include "core/wallhaven_client.hpp"
#include "core/wallpaper_apply.hpp"
#define namespace namespace_
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#undef namespace

#include <GLES2/gl2.h>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
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

WaylandApp::WaylandApp(AppConfig config, Database &db, DisplayMode mode, bool benchmark_ready)
    : config_(std::move(config)), db_(db), mode_(mode), benchmark_ready_(benchmark_ready) {}

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
    for (const auto &entry : db_.cached_wallhaven()) {
      Wallpaper item;
      item.path = entry.image_url;
      item.name = "wallhaven-" + entry.id;
      item.type = WallpaperType::Wallhaven;
      item.thumb_path = entry.thumb_path;
      item.width = entry.width;
      item.height = entry.height;
      item.added_at = entry.cached_at;
      wallpapers_.push_back(std::move(item));
    }
    if (selected_ >= static_cast<int>(wallpapers_.size())) {
      selected_ = std::max(0, static_cast<int>(wallpapers_.size()) - 1);
    }
    return;
  }
  Filter filter;
  filter.query = query_;
  wallpapers_ = db_.list_wallpapers(filter);
  if (selected_ >= static_cast<int>(wallpapers_.size())) {
    selected_ = std::max(0, static_cast<int>(wallpapers_.size()) - 1);
  }
}

void WaylandApp::redraw() {
  load_wallpapers();
  renderer_.render(wallpapers_, selected_, mode_, query_, wallhaven_mode_, status_);
  eglSwapBuffers(egl_display_, egl_surface_);
}

void WaylandApp::apply_selected() {
  if (wallpapers_.empty() || selected_ < 0 || selected_ >= static_cast<int>(wallpapers_.size())) {
    return;
  }
  ApplyResult result;
  if (wallpapers_[selected_].type == WallpaperType::Wallhaven) {
    WallhavenEntry match;
    for (const auto &entry : db_.cached_wallhaven()) {
      if (entry.image_url == wallpapers_[selected_].path ||
          "wallhaven-" + entry.id == wallpapers_[selected_].name) {
        match = entry;
        break;
      }
    }
    if (match.id.empty()) {
      result = {false, "wallhaven entry not found"};
    } else {
      try {
        const auto downloaded = wallhaven_download(config_, match);
        result = apply_path(db_, config_, downloaded.string(), WallpaperType::Image);
      } catch (const std::exception &err) {
        result = {false, err.what()};
      }
    }
  } else {
    result = apply_wallpaper(db_, config_, wallpapers_[selected_]);
  }
  if (!result.ok) {
    std::cerr << "apply failed: " << result.message << '\n';
    status_ = "APPLY FAILED";
    redraw();
  } else if (config_.close_on_selection) {
    running_ = false;
  }
}

void WaylandApp::toggle_favorite() {
  if (wallpapers_.empty() || selected_ < 0 || selected_ >= static_cast<int>(wallpapers_.size())) {
    return;
  }
  const bool next = !wallpapers_[selected_].favorite;
  db_.set_favorite(wallpapers_[selected_].path, next);
  redraw();
}

void WaylandApp::load_wallhaven() {
  if (!config_.enable_wallhaven) {
    status_ = "WALLHAVEN DISABLED";
    redraw();
    return;
  }
  const std::string search = query_.empty() ? config_.wallhaven_default_query : query_;
  try {
    cache_wallhaven_search(db_, config_, search, wallhaven_page_);
    wallhaven_mode_ = true;
    selected_ = 0;
    status_ = "WALLHAVEN " + search.substr(0, 28);
  } catch (const std::exception &err) {
    status_ = "WALLHAVEN ERROR";
    std::cerr << "wallhaven failed: " << err.what() << '\n';
  }
  redraw();
}

void WaylandApp::show_local() {
  wallhaven_mode_ = false;
  status_ = "LOCAL WALLPAPERS";
  selected_ = 0;
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
  }
  redraw();
}

void WaylandApp::set_query(std::string query) {
  query_ = std::move(query);
  selected_ = 0;
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
  load_wallpapers();
  redraw();
  mark_benchmark_ready();
  while (running_ && wl_display_dispatch(display_) != -1) {
  }
  return 0;
}

void WaylandApp::cleanup() {
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
    case PickerAction::Apply:
      app->apply_selected();
      return;
    case PickerAction::None:
      break;
    }
    const int hit = app->renderer_.hit_test(app->pointer_x_, app->pointer_y_);
    if (hit >= 0) {
      app->selected_ = hit;
      app->apply_selected();
    } else if (!app->renderer_.stage_contains(app->pointer_x_, app->pointer_y_)) {
      app->running_ = false;
    }
  }
}
void WaylandApp::pointer_axis(void *data, wl_pointer *, uint32_t, uint32_t, wl_fixed_t value) {
  auto *app = static_cast<WaylandApp *>(data);
  app->move_selection(wl_fixed_to_double(value) > 0 ? 1 : -1);
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
  app->xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  app->xkb_keymap_ = xkb_keymap_new_from_string(app->xkb_context_, map,
                                                XKB_KEYMAP_FORMAT_TEXT_V1,
                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
  app->xkb_state_ = xkb_state_new(app->xkb_keymap_);
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
    if (n > 0 && text[0] >= 32 && text[0] < 127) {
      app->query_ += text;
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
    app->move_selection(-4);
    return;
  }
  if (sym == XKB_KEY_Down) {
    app->move_selection(4);
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
