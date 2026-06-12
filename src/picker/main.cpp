#include "core/config.hpp"
#include "core/database.hpp"
#include "picker/wayland_app.hpp"

#include <iostream>
#include <stdexcept>

using namespace vibewall;

int main(int argc, char **argv) {
  try {
    DisplayMode mode = DisplayMode::Slice;
    bool benchmark_ready = false;
    bool start_wallhaven = false;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--mode" && i + 1 < argc) {
        mode = display_mode_from_string(argv[++i]);
      } else if (arg == "--benchmark-ready") {
        benchmark_ready = true;
      } else if (arg == "--wallhaven") {
        start_wallhaven = true;
      }
    }

    write_default_config_if_missing();
    AppConfig config = load_config();
    Database db(config.db_path);
    db.migrate();
    picker::WaylandApp app(config, db, mode, benchmark_ready, start_wallhaven);
    return app.run();
  } catch (const std::exception &err) {
    std::cerr << "vibewall-picker: " << err.what() << '\n';
    return 1;
  }
}
