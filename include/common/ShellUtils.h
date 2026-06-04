#pragma once
#include <string>

namespace ft {

std::string shell_exec(const char* cmd);
std::string read_sysfs(const std::string& path);
std::string read_file(const std::string& path);

} // namespace ft
