#include "core/process.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace vibewall {

namespace {
std::vector<char *> argv_ptrs(const std::vector<std::string> &argv) {
  std::vector<char *> out;
  out.reserve(argv.size() + 1);
  for (const std::string &arg : argv) {
    out.push_back(const_cast<char *>(arg.c_str()));
  }
  out.push_back(nullptr);
  return out;
}

std::string replace_all(std::string value, const std::string &from, const std::string &to) {
  std::size_t pos = 0;
  while ((pos = value.find(from, pos)) != std::string::npos) {
    value.replace(pos, from.size(), to);
    pos += to.size();
  }
  return value;
}
} // namespace

ProcessResult run_process(const std::vector<std::string> &argv) {
  if (argv.empty() || argv.front().empty()) {
    return {127, "empty command"};
  }

  const pid_t pid = fork();
  if (pid < 0) {
    return {127, std::strerror(errno)};
  }
  if (pid == 0) {
    auto ptrs = argv_ptrs(argv);
    execvp(ptrs[0], ptrs.data());
    _exit(127);
  }

  int status = 0;
  pid_t waited = 0;
  do {
    waited = waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited < 0) {
    return {127, std::strerror(errno)};
  }
  if (WIFEXITED(status)) {
    return {WEXITSTATUS(status), {}};
  }
  if (WIFSIGNALED(status)) {
    return {128 + WTERMSIG(status), "terminated by signal"};
  }
  return {127, "process did not exit normally"};
}

ProcessResult spawn_detached(const std::vector<std::string> &argv) {
  if (argv.empty() || argv.front().empty()) {
    return {127, "empty command"};
  }
  const pid_t pid = fork();
  if (pid < 0) {
    return {127, std::strerror(errno)};
  }
  if (pid == 0) {
    const pid_t grandchild = fork();
    if (grandchild < 0) {
      _exit(127);
    }
    if (grandchild > 0) {
      _exit(0);
    }
    setsid();
    const int null_fd = open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
      dup2(null_fd, STDIN_FILENO);
      dup2(null_fd, STDOUT_FILENO);
      dup2(null_fd, STDERR_FILENO);
      if (null_fd > STDERR_FILENO) {
        close(null_fd);
      }
    }
    auto ptrs = argv_ptrs(argv);
    execvp(ptrs[0], ptrs.data());
    _exit(127);
  }

  int status = 0;
  pid_t waited = 0;
  do {
    waited = waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited < 0) {
    return {127, std::strerror(errno)};
  }
  if (WIFEXITED(status)) {
    return {WEXITSTATUS(status), {}};
  }
  if (WIFSIGNALED(status)) {
    return {128 + WTERMSIG(status), "terminated by signal"};
  }
  return {127, "launcher did not exit normally"};
}

std::vector<std::string> substitute_args(const std::vector<std::string> &args,
                                         const std::string &path,
                                         const std::string &type,
                                         const std::string &name,
                                         const std::string &monitor) {
  std::vector<std::string> out;
  out.reserve(args.size());
  const std::string monitor_or_star = monitor.empty() ? "*" : monitor;
  for (std::string arg : args) {
    arg = replace_all(arg, "%path%", path);
    arg = replace_all(arg, "%type%", type);
    arg = replace_all(arg, "%name%", name);
    arg = replace_all(arg, "%monitor_or_star%", monitor_or_star);
    if (arg == "%monitor%" && monitor.empty()) {
      continue;
    }
    arg = replace_all(arg, "%monitor%", monitor);
    out.push_back(arg);
  }
  return out;
}

} // namespace vibewall
