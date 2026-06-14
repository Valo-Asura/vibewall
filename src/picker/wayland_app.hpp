#pragma once

#include "core/config.hpp"
#include "core/database.hpp"
#include "picker/renderer.hpp"

#include <EGL/egl.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <xkbcommon/xkbcommon.h>

struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;

namespace vibewall::picker {

class WaylandApp {
public:
  WaylandApp(AppConfig config, Database &db, DisplayMode mode, bool benchmark_ready,
             bool start_wallhaven = false);
  ~WaylandApp();
  WaylandApp(const WaylandApp &) = delete;
  WaylandApp &operator=(const WaylandApp &) = delete;

  int run();

  static void registry_global(void *data, wl_registry *registry, uint32_t name,
                              const char *interface, uint32_t version);
  static void registry_remove(void *data, wl_registry *registry, uint32_t name);
  static void layer_configure(void *data, zwlr_layer_surface_v1 *surface, uint32_t serial,
                              uint32_t width, uint32_t height);
  static void layer_closed(void *data, zwlr_layer_surface_v1 *surface);
  static void seat_capabilities(void *data, wl_seat *seat, uint32_t capabilities);
  static void seat_name(void *data, wl_seat *seat, const char *name);
  static void pointer_enter(void *data, wl_pointer *pointer, uint32_t serial, wl_surface *surface,
                            wl_fixed_t sx, wl_fixed_t sy);
  static void pointer_leave(void *data, wl_pointer *pointer, uint32_t serial, wl_surface *surface);
  static void pointer_motion(void *data, wl_pointer *pointer, uint32_t time, wl_fixed_t sx,
                             wl_fixed_t sy);
  static void pointer_button(void *data, wl_pointer *pointer, uint32_t serial, uint32_t time,
                             uint32_t button, uint32_t state);
  static void pointer_axis(void *data, wl_pointer *pointer, uint32_t time, uint32_t axis,
                           wl_fixed_t value);
  static void pointer_frame(void *data, wl_pointer *pointer);
  static void pointer_axis_source(void *data, wl_pointer *pointer, uint32_t axis_source);
  static void pointer_axis_stop(void *data, wl_pointer *pointer, uint32_t time, uint32_t axis);
  static void pointer_axis_discrete(void *data, wl_pointer *pointer, uint32_t axis,
                                    int32_t discrete);
  static void pointer_axis_value120(void *data, wl_pointer *pointer, uint32_t axis,
                                    int32_t value120);
  static void pointer_axis_relative_direction(void *data, wl_pointer *pointer, uint32_t axis,
                                              uint32_t direction);
  static void keyboard_keymap(void *data, wl_keyboard *keyboard, uint32_t format, int fd,
                              uint32_t size);
  static void keyboard_enter(void *data, wl_keyboard *keyboard, uint32_t serial,
                             wl_surface *surface, wl_array *keys);
  static void keyboard_leave(void *data, wl_keyboard *keyboard, uint32_t serial,
                             wl_surface *surface);
  static void keyboard_key(void *data, wl_keyboard *keyboard, uint32_t serial, uint32_t time,
                           uint32_t key, uint32_t state);
  static void keyboard_modifiers(void *data, wl_keyboard *keyboard, uint32_t serial,
                                 uint32_t depressed, uint32_t latched, uint32_t locked,
                                 uint32_t group);
  static void keyboard_repeat(void *data, wl_keyboard *keyboard, int32_t rate, int32_t delay);

private:
  AppConfig config_;
  Database &db_;
  DisplayMode mode_;
  bool benchmark_ready_ = false;
  bool start_wallhaven_ = false;
  bool running_ = true;
  bool configured_ = false;
  bool search_active_ = false;
  bool wallhaven_mode_ = false;
  bool favorites_only_ = false;
  bool wallhaven_sfw_ = true;
  bool data_dirty_ = true;
  std::atomic<bool> wallhaven_loading_ = false;
  std::atomic<bool> wallhaven_done_ = false;
  int wallhaven_page_ = 1;
  std::string wallhaven_sort_ = "toplist"; // toplist | date_added | views | hot
  std::optional<WallpaperType> type_filter_;
  std::optional<ColorGroup> color_filter_;
  std::string query_;
  std::string status_;
  std::string background_path_;
  std::vector<Wallpaper> wallpapers_;
  int selected_ = 0;       // which item is highlighted/will-be-applied on click
  int scroll_offset_ = 0;  // viewport anchor: which item is in the "center" row

  wl_display *display_ = nullptr;
  wl_registry *registry_ = nullptr;
  wl_compositor *compositor_ = nullptr;
  wl_seat *seat_ = nullptr;
  wl_pointer *pointer_ = nullptr;
  wl_keyboard *keyboard_ = nullptr;
  wl_surface *surface_ = nullptr;
  zwlr_layer_shell_v1 *layer_shell_ = nullptr;
  zwlr_layer_surface_v1 *layer_surface_ = nullptr;
  wl_egl_window *egl_window_ = nullptr;
  EGLDisplay egl_display_ = EGL_NO_DISPLAY;
  EGLContext egl_context_ = EGL_NO_CONTEXT;
  EGLSurface egl_surface_ = EGL_NO_SURFACE;
  xkb_context *xkb_context_ = nullptr;
  xkb_keymap *xkb_keymap_ = nullptr;
  xkb_state *xkb_state_ = nullptr;
  int width_ = 1280;
  int height_ = 760;
  float pointer_x_ = 0.0F;
  float pointer_y_ = 0.0F;
  double scroll_accumulator_ = 0.0;
  Renderer renderer_;
  std::thread wallhaven_worker_;
  std::mutex wallhaven_mutex_;
  bool wallhaven_pending_success_ = false;
  int wallhaven_pending_page_ = 1;
  std::string wallhaven_pending_status_;

  void connect();
  void setup_egl();
  void load_wallpapers();
  void refresh_background_path();
  void mark_data_dirty();
  void redraw();
  void apply_selected();
  std::optional<WallhavenEntry> selected_wallhaven_entry() const;
  void download_selected_wallhaven(bool apply_after_download);
  void toggle_favorite();
  void toggle_favorite_at(int index);
  void load_wallhaven();
  void load_wallhaven_next_page();
  void set_wallhaven_sort(const std::string &sort);
  void start_wallhaven_load(int page, bool reset_view, std::string loading_status);
  void finish_wallhaven_load_if_ready();
  void show_local();
  void apply_random();
  void move_selection(int delta);
  void scroll_viewport(int delta);
  int scroll_step() const;
  int visible_rows_count() const;
  void set_type_filter(std::optional<WallpaperType> type);
  void set_color_filter(std::optional<ColorGroup> color);
  void set_query(std::string query);
  void mark_benchmark_ready() const;
  void cleanup();

};

} // namespace vibewall::picker
