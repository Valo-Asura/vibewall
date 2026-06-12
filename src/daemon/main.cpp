#include "core/app_paths.hpp"
#include "core/config.hpp"
#include "core/database.hpp"
#include "core/process.hpp"
#include "core/wallpaper_apply.hpp"

#include <csignal>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace vibewall;

namespace {
bool running = true;
pid_t picker_pid = -1;

void handle_signal(int) {
  running = false;
}

void install_signal_handlers() {
  struct sigaction action {};
  action.sa_handler = handle_signal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
}

void reap_picker() {
  int status = 0;
  while (true) {
    const pid_t result = waitpid(-1, &status, WNOHANG);
    if (result <= 0) {
      break;
    }
    if (result == picker_pid) {
      picker_pid = -1;
    }
  }
  if (picker_pid > 0 && kill(picker_pid, 0) != 0 && errno == ESRCH) {
    picker_pid = -1;
  }
}

bool picker_is_running() {
  reap_picker();
  return picker_pid > 0 && kill(picker_pid, 0) == 0;
}

void open_picker() {
  if (picker_is_running()) {
    return;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error("failed to fork picker");
  }
  if (pid == 0) {
    execlp("vibewall-picker", "vibewall-picker", nullptr);
    _exit(127);
  }
  picker_pid = pid;
}

void close_picker() {
  reap_picker();
  if (picker_pid > 0) {
    kill(picker_pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 30; ++i) {
      const pid_t result = waitpid(picker_pid, &status, WNOHANG);
      if (result == picker_pid || (result < 0 && errno == ECHILD)) {
        picker_pid = -1;
        break;
      }
      usleep(10000);
    }
    if (picker_pid > 0) {
      kill(picker_pid, SIGKILL);
      (void)waitpid(picker_pid, &status, 0);
    }
    picker_pid = -1;
  }
  run_process({"pkill", "-x", "vibewall-picker"});
  run_process({"pkill", "-f", "vibewall-picker"});
  reap_picker();
}

std::string handle_command(const std::string &cmd, Database &db, const AppConfig &config) {
  reap_picker();
  if (cmd == "status") {
    return "{\"ok\":true,\"daemon\":\"running\",\"picker_pid\":" + std::to_string(picker_pid) + "}\n";
  }
  if (cmd == "open") {
    open_picker();
    return "opened\n";
  }
  if (cmd == "close") {
    close_picker();
    return "closed\n";
  }
  if (cmd == "toggle") {
    if (picker_is_running()) {
      close_picker();
      return "closed\n";
    }
    open_picker();
    return "opened\n";
  }
  if (cmd == "restore") {
    const auto result = restore_last_wallpaper(db, config);
    return result.message + "\n";
  }
  if (cmd == "random") {
    const auto result = apply_random_wallpaper(db, config);
    return result.message + "\n";
  }
  return "error: unknown command\n";
}
} // namespace

int main() {
  try {
    install_signal_handlers();
    write_default_config_if_missing();
    AppConfig config = load_config();
    Database db(config.db_path);
    db.migrate();

    const auto socket_path = runtime_socket_path();
    std::error_code ec;
    std::filesystem::remove(socket_path, ec);

    const int server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server < 0) {
      throw std::runtime_error("socket failed");
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const auto path_string = socket_path.string();
    if (path_string.size() >= sizeof(addr.sun_path)) {
      throw std::runtime_error("socket path too long");
    }
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path_string.c_str());
    if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
      throw std::runtime_error("bind failed: " + std::string(std::strerror(errno)));
    }
    if (listen(server, 8) != 0) {
      throw std::runtime_error("listen failed");
    }

    while (running) {
      const int client = accept(server, nullptr, nullptr);
      if (client < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw std::runtime_error("accept failed");
      }
      char buffer[2048];
      ssize_t n = read(client, buffer, sizeof(buffer) - 1);
      if (n > 0) {
        buffer[n] = '\0';
        std::string cmd(buffer);
        if (!cmd.empty() && cmd.back() == '\n') {
          cmd.pop_back();
        }
        const std::string response = handle_command(cmd, db, config);
        (void)write(client, response.c_str(), response.size());
      }
      close(client);
    }

    close(server);
    std::filesystem::remove(socket_path, ec);
    return 0;
  } catch (const std::exception &err) {
    std::cerr << "vibewall-daemon: " << err.what() << '\n';
    return 1;
  }
}
