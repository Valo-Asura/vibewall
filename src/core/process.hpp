#pragma once

#include <string>
#include <vector>

namespace vibewall {

struct ProcessResult {
  int exit_code = -1;
  std::string error;
};

ProcessResult run_process(const std::vector<std::string> &argv);
ProcessResult spawn_detached(const std::vector<std::string> &argv);
std::vector<std::string> substitute_args(const std::vector<std::string> &args,
                                         const std::string &path,
                                         const std::string &type,
                                         const std::string &name,
                                         const std::string &monitor);

} // namespace vibewall
