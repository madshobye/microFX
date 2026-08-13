#include "project_protocol.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace microfx {
namespace {

bool atomic_write(const fs::path& path, const std::string& value) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) return false;
  fs::path temporary = path;
  temporary += ".new";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output) return false;
  }
  fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write |
                               fs::perms::group_read | fs::perms::others_read,
                  fs::perm_options::replace, ec);
  fs::rename(temporary, path, ec);
  return !ec;
}

std::optional<fs::path> asset_path(const fs::path& project_root, const char* name) {
  if (!name || !*name) return std::nullopt;
  fs::path relative(name);
  if (relative.is_absolute()) return std::nullopt;
  relative = relative.lexically_normal();
  for (const auto& part : relative) {
    if (part == "..") return std::nullopt;
  }
  return project_root / "assets" / relative;
}

std::optional<std::string> decode_base64(const char* input) {
  if (!input) return std::nullopt;
  static constexpr signed char table[128] = {
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
      -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
      52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
      -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
      15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
      -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
      41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
  };
  std::string output;
  unsigned value = 0;
  int bits = -8;
  for (const unsigned char ch : std::string(input)) {
    if (ch >= 128) return std::nullopt;
    int decoded = table[ch];
    if (decoded == -2) break;
    if (decoded < 0) return std::nullopt;
    value = (value << 6) | static_cast<unsigned>(decoded);
    bits += 6;
    if (bits >= 0) {
      output.push_back(static_cast<char>((value >> bits) & 0xff));
      bits -= 8;
    }
  }
  return output;
}

std::string encode_base64(const std::string& input) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve(((input.size() + 2) / 3) * 4);
  for (size_t offset = 0; offset < input.size(); offset += 3) {
    const size_t remaining = input.size() - offset;
    const unsigned value =
        (static_cast<unsigned char>(input[offset]) << 16) |
        (remaining > 1 ? static_cast<unsigned char>(input[offset + 1]) << 8 : 0) |
        (remaining > 2 ? static_cast<unsigned char>(input[offset + 2]) : 0);
    output.push_back(alphabet[(value >> 18) & 0x3f]);
    output.push_back(alphabet[(value >> 12) & 0x3f]);
    output.push_back(remaining > 1 ? alphabet[(value >> 6) & 0x3f] : '=');
    output.push_back(remaining > 2 ? alphabet[value & 0x3f] : '=');
  }
  return output;
}

cJSON* response_base(cJSON* request, const char* type = "ack") {
  cJSON* response = cJSON_CreateObject();
  cJSON_AddStringToObject(response, "type", type);
  cJSON_AddBoolToObject(response, "ok", true);
  cJSON_AddNumberToObject(response, "version", 1);
  if (cJSON* id = cJSON_GetObjectItemCaseSensitive(request, "id"); cJSON_IsString(id))
    cJSON_AddStringToObject(response, "id", id->valuestring);
  return response;
}

void fail(cJSON* response, const char* message) {
  cJSON_ReplaceItemInObject(response, "ok", cJSON_CreateFalse());
  cJSON_AddStringToObject(response, "error", message);
}

}  // namespace

std::string read_text(const fs::path& path, const std::string& fallback) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return fallback;
  std::ostringstream out;
  out << input.rdbuf();
  std::string value = out.str();
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
  return value.empty() ? fallback : value;
}

cJSON* handle_project_command(const char* data, size_t length,
                              const fs::path& project_root,
                              const fs::path& reload_signal) {
  cJSON* request = cJSON_ParseWithLength(data, length);
  if (!request) {
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "error");
    cJSON_AddBoolToObject(response, "ok", false);
    cJSON_AddStringToObject(response, "error", "invalid JSON");
    cJSON_AddNumberToObject(response, "version", 1);
    return response;
  }

  cJSON* response = response_base(request);
  cJSON* type = cJSON_GetObjectItemCaseSensitive(request, "type");
  const char* command = cJSON_IsString(type) ? type->valuestring : "";

  if (std::strcmp(command, "project.get") == 0) {
    cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("project"));
    std::string code = read_text(project_root / "main.js");
    cJSON_AddStringToObject(response, "code", code.c_str());
    cJSON* assets = cJSON_AddArrayToObject(response, "assets");
    std::error_code ec;
    fs::path root = project_root / "assets";
    std::vector<fs::path> paths;
    if (fs::exists(root, ec)) {
      for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) paths.push_back(it->path());
      }
      std::sort(paths.begin(), paths.end());
      for (const fs::path& path : paths) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "path", fs::relative(path, root, ec).generic_string().c_str());
        cJSON_AddNumberToObject(item, "size", static_cast<double>(fs::file_size(path, ec)));
        cJSON_AddItemToArray(assets, item);
      }
    }
  } else if (std::strcmp(command, "code.put") == 0) {
    cJSON* content = cJSON_GetObjectItemCaseSensitive(request, "content");
    if (!cJSON_IsString(content) || !atomic_write(project_root / "main.js", content->valuestring))
      fail(response, "could not write main.js");
  } else if (std::strcmp(command, "asset.get") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    auto source = asset_path(project_root, cJSON_IsString(path) ? path->valuestring : nullptr);
    if (!source) {
      fail(response, "invalid asset path");
    } else {
      std::ifstream input(*source, std::ios::binary);
      if (!input) {
        fail(response, "could not read asset");
      } else {
        std::ostringstream bytes;
        bytes << input.rdbuf();
        if (!input.eof() && input.fail()) {
          fail(response, "could not read asset");
        } else {
          std::string content = bytes.str();
          std::string encoded = encode_base64(content);
          cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("asset"));
          cJSON_AddStringToObject(response, "path", path->valuestring);
          cJSON_AddNumberToObject(response, "size", static_cast<double>(content.size()));
          cJSON_AddStringToObject(response, "encoding", "base64");
          cJSON_AddStringToObject(response, "content", encoded.c_str());
        }
      }
    }
  } else if (std::strcmp(command, "asset.put") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    cJSON* content = cJSON_GetObjectItemCaseSensitive(request, "content");
    auto destination = asset_path(project_root, cJSON_IsString(path) ? path->valuestring : nullptr);
    if (!destination || !cJSON_IsString(content)) {
      fail(response, "invalid asset");
    } else {
      auto decoded = decode_base64(content->valuestring);
      if (!decoded || !atomic_write(*destination, *decoded)) fail(response, "could not write asset");
    }
  } else if (std::strcmp(command, "asset.delete") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    auto destination = asset_path(project_root, cJSON_IsString(path) ? path->valuestring : nullptr);
    std::error_code ec;
    if (!destination || (!fs::remove(*destination, ec) && ec)) fail(response, "could not delete asset");
  } else if (std::strcmp(command, "project.activate") == 0) {
    if (!atomic_write(reload_signal, "1\n")) fail(response, "could not request project reload");
  } else {
    fail(response, "unknown command");
  }

  cJSON_Delete(request);
  return response;
}

}  // namespace microfx
