#include "project_protocol.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path configured_path(const char* name,
                                      const std::filesystem::path& fallback) {
  const char* value = std::getenv(name);
  return value && *value ? std::filesystem::path(value) : fallback;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: protocol-cli SANDBOX\n";
    return 2;
  }
  const std::filesystem::path sandbox = argv[1];
  const std::filesystem::path apps = configured_path(
      "MICROFX_TEST_APPS_ROOT", sandbox / "apps");
  const std::filesystem::path reload = configured_path(
      "MICROFX_TEST_RELOAD_SIGNAL", sandbox / "run" / "reload");
  const std::filesystem::path status = configured_path(
      "MICROFX_TEST_RELOAD_STATUS", sandbox / "run" / "status");
  const std::filesystem::path console = configured_path(
      "MICROFX_TEST_CONSOLE_LOG", sandbox / "canvas.log");
  std::string request;
  while (std::getline(std::cin, request)) {
    cJSON* response = microfx::handle_project_command(
        request.data(), request.size(), apps, reload, console, status);
    char* printed = cJSON_PrintUnformatted(response);
    std::cout << (printed ? printed : "{}") << '\n' << std::flush;
    cJSON_free(printed);
    cJSON_Delete(response);
  }
  return 0;
}
