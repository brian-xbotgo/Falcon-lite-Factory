#pragma once

#include <string>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <unistd.h>

namespace ft {

struct ShellResult {
    int         exit_code = -1; 
    std::string output;
};

inline ShellResult shell_exec(const std::string& cmd) {
    ShellResult result;                         
    std::string full_cmd = cmd + " 2>&1";       
    FILE* pipe = popen(full_cmd.c_str(), "r");  
    if (!pipe) { result.output = "popen failed"; return result; }
    std::array<char, 256> buf; 
    while (std::fgets(buf.data(), buf.size(), pipe)) result.output += buf.data(); 
    int status = pclose(pipe);
    if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status); 
    else result.exit_code = -1;
    return result;
}

inline bool shell_ok(const std::string& cmd) { return shell_exec(cmd).exit_code == 0; }
inline bool shell_output_contains(const std::string& cmd, const std::string& keyword) {
    auto r = shell_exec(cmd);
    return r.output.find(keyword) != std::string::npos;
}

inline std::string read_sysfs(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return "";
    std::array<char, 256> buf = {};
    std::fgets(buf.data(), buf.size(), f);
    std::fclose(f);
    std::string s(buf.data());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

inline std::string read_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return "";
    std::string content;
    std::array<char, 512> buf;
    while (std::fgets(buf.data(), buf.size(), f)) content += buf.data();
    std::fclose(f);
    return content;
}

inline std::string uevent_get(const std::string& content, const std::string& key) {
    std::string prefix = key + "=";
    auto pos = content.find(prefix);
    if (pos == std::string::npos) return "";
    pos += prefix.size();
    auto end = content.find('\n', pos);
    if (end == std::string::npos) end = content.size();
    return content.substr(pos, end - pos);
}

inline bool file_contains(const std::string& path, const std::string& keyword) {
    return read_file(path).find(keyword) != std::string::npos;
}

inline bool process_running(const std::string& name) {
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd), "pidof %s > /dev/null 2>&1", name.c_str());
    return std::system(cmd) == 0;
}

inline bool general_test(const std::string& process_name,
                         const std::string& start_cmd,
                         const std::string& verify_path,
                         const std::string& target_pattern,
                         int timeout_sec = 30) {
    std::system(start_cmd.c_str());
    for (int i = 0; i < timeout_sec * 2; ++i) {
        if (file_contains(verify_path, target_pattern)) {
            char cmd[256];
            std::snprintf(cmd, sizeof(cmd), "killall %s 2>/dev/null", process_name.c_str());
            std::system(cmd);
            return true;
        }
        if (!process_running(process_name)) {
            usleep(200 * 1000);
            return file_contains(verify_path, target_pattern);
        }
        usleep(500 * 1000);
    }
    char cmd[256];
    std::snprintf(cmd, sizeof(cmd), "killall %s 2>/dev/null", process_name.c_str());
    std::system(cmd);
    return false;
}

} // namespace ft
