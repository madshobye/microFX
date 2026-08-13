#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

extern "C" {
#include <cjson/cJSON.h>
}

namespace microfx {

std::string read_text(const std::filesystem::path& path,
                      const std::string& fallback = {});

cJSON* handle_project_command(const char* data, size_t length,
                              const std::filesystem::path& project_root,
                              const std::filesystem::path& reload_signal);

}  // namespace microfx
