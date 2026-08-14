#include "project_protocol.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace microfx {
namespace {

constexpr size_t kRevisionLimit = 20;
constexpr size_t kConsoleReadLimit = 32768;
constexpr size_t kRequestLimit = 24u * 1024u * 1024u;
constexpr size_t kAssetLimit = 16u * 1024u * 1024u;
constexpr size_t kCodeLimit = 512u * 1024u;
constexpr size_t kAssetChunkLimit = 128u * 1024u;

bool path_within(const fs::path& base, const fs::path& candidate) {
  auto base_part = base.begin();
  auto candidate_part = candidate.begin();
  for (; base_part != base.end(); ++base_part, ++candidate_part) {
    if (candidate_part == candidate.end() || *base_part != *candidate_part) return false;
  }
  return true;
}

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

bool valid_name(const std::string& name) {
  if (name.empty() || name.size() > 64 || name.front() == '.' || name.back() == '.') return false;
  return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
    return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
  });
}

bool valid_token(const std::string& token) {
  if (token.empty() || token.size() > 128) return false;
  return std::all_of(token.begin(), token.end(), [](unsigned char ch) {
    return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == ':';
  });
}

std::optional<std::string> requested_name(cJSON* request) {
  cJSON* value = cJSON_GetObjectItemCaseSensitive(request, "project");
  if (!cJSON_IsString(value)) return std::string{};
  std::string name(value->valuestring);
  if (!valid_name(name)) return std::nullopt;
  return name;
}

std::string active_name(const fs::path& apps_root) {
  std::error_code ec;
  fs::path link = fs::read_symlink(apps_root / "current", ec);
  if (ec) return {};
  return link.filename().string();
}

std::optional<fs::path> project_path(const fs::path& apps_root, cJSON* request,
                                     std::string* selected_name = nullptr) {
  auto requested = requested_name(request);
  if (!requested) return std::nullopt;
  std::string name = requested->empty() ? active_name(apps_root) : *requested;
  if (!valid_name(name)) return std::nullopt;
  if (selected_name) *selected_name = name;
  std::error_code ec;
  fs::path projects = fs::weakly_canonical(apps_root / "projects", ec);
  if (ec) return std::nullopt;
  fs::path candidate = fs::weakly_canonical(projects / name, ec);
  if (ec || !path_within(projects, candidate)) return std::nullopt;
  fs::file_status status = fs::symlink_status(projects / name, ec);
  if (!ec && fs::is_symlink(status)) return std::nullopt;
  return candidate;
}

std::optional<fs::path> asset_path(const fs::path& project_root, const char* name) {
  if (!name || !*name) return std::nullopt;
  fs::path relative(name);
  if (relative.is_absolute()) return std::nullopt;
  relative = relative.lexically_normal();
  for (const auto& part : relative) {
    if (part == "..") return std::nullopt;
  }
  std::error_code ec;
  fs::path project = fs::weakly_canonical(project_root, ec);
  if (ec) return std::nullopt;
  fs::path assets = fs::weakly_canonical(project / "assets", ec);
  if (ec || !path_within(project, assets)) return std::nullopt;
  fs::path lexical = assets / relative;
  fs::file_status status = fs::symlink_status(lexical, ec);
  if (!ec && fs::is_symlink(status)) return std::nullopt;
  ec.clear();
  fs::path candidate = fs::weakly_canonical(lexical, ec);
  if (ec || !path_within(assets, candidate)) return std::nullopt;
  return candidate;
}

std::optional<fs::path> upload_path(const fs::path& project_root,
                                    const std::string& token,
                                    const char* suffix) {
  if (!valid_token(token)) return std::nullopt;
  std::error_code ec;
  fs::path project = fs::weakly_canonical(project_root, ec);
  if (ec) return std::nullopt;
  fs::path uploads = project / ".uploads";
  fs::file_status status = fs::symlink_status(uploads, ec);
  if (!ec && fs::is_symlink(status)) return std::nullopt;
  ec.clear();
  fs::create_directories(uploads, ec);
  if (ec) return std::nullopt;
  uploads = fs::weakly_canonical(uploads, ec);
  if (ec || !path_within(project, uploads)) return std::nullopt;
  return uploads / (token + suffix);
}

std::optional<size_t> requested_size(cJSON* request, const char* field,
                                     size_t maximum) {
  cJSON* value = cJSON_GetObjectItemCaseSensitive(request, field);
  if (!cJSON_IsNumber(value) || value->valuedouble < 0 ||
      value->valuedouble > static_cast<double>(maximum)) return std::nullopt;
  size_t result = static_cast<size_t>(value->valuedouble);
  if (static_cast<double>(result) != value->valuedouble) return std::nullopt;
  return result;
}

std::string upload_metadata(const char* path, size_t size) {
  return std::string(path ? path : "") + "\n" + std::to_string(size) + "\n";
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
  cJSON_AddNumberToObject(response, "version", 2);
  if (cJSON* id = cJSON_GetObjectItemCaseSensitive(request, "id"); cJSON_IsString(id))
    cJSON_AddStringToObject(response, "id", id->valuestring);
  return response;
}

void fail(cJSON* response, const char* message) {
  cJSON_ReplaceItemInObject(response, "ok", cJSON_CreateFalse());
  cJSON_AddStringToObject(response, "error", message);
}

std::vector<fs::path> regular_files(const fs::path& root) {
  std::vector<fs::path> paths;
  std::error_code ec;
  if (!fs::exists(root, ec)) return paths;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    fs::file_status status = it->symlink_status(ec);
    if (!ec && fs::is_regular_file(status)) paths.push_back(it->path());
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

void append_assets(cJSON* response, const fs::path& project_root) {
  cJSON* assets = cJSON_AddArrayToObject(response, "assets");
  fs::path root = project_root / "assets";
  std::error_code ec;
  for (const fs::path& path : regular_files(root)) {
    cJSON* item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "path", fs::relative(path, root, ec).generic_string().c_str());
    cJSON_AddNumberToObject(item, "size", static_cast<double>(fs::file_size(path, ec)));
    cJSON_AddItemToArray(assets, item);
  }
}

void append_asset_folders(cJSON* response, const fs::path& project_root) {
  cJSON* folders = cJSON_AddArrayToObject(response, "folders");
  fs::path root = project_root / "assets";
  std::error_code ec;
  std::vector<fs::path> paths;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end;
       it.increment(ec)) {
    fs::file_status status = it->symlink_status(ec);
    if (!ec && fs::is_directory(status)) paths.push_back(it->path());
  }
  std::sort(paths.begin(), paths.end());
  for (const fs::path& path : paths) {
    const std::string relative = fs::relative(path, root, ec).generic_string();
    if (!ec && relative != ".")
      cJSON_AddItemToArray(folders, cJSON_CreateString(relative.c_str()));
  }
}

std::optional<fs::path> revision_root_path(const fs::path& project_root) {
  std::error_code ec;
  fs::path project = fs::weakly_canonical(project_root, ec);
  if (ec) return std::nullopt;
  fs::path lexical = project / "revisions";
  fs::file_status status = fs::symlink_status(lexical, ec);
  if (!ec && fs::is_symlink(status)) return std::nullopt;
  ec.clear();
  fs::path root = fs::weakly_canonical(lexical, ec);
  if (ec || !path_within(project, root)) return std::nullopt;
  return root;
}

std::vector<fs::path> revisions(const fs::path& project_root) {
  std::vector<fs::path> result;
  auto safe_root = revision_root_path(project_root);
  if (!safe_root) return result;
  fs::path root = *safe_root;
  std::error_code ec;
  if (!fs::exists(root, ec)) return result;
  for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    const bool snapshot = it->is_directory(ec) && valid_name(name) && name.front() == 'r';
    const bool legacy = it->is_regular_file(ec) && it->path().extension() == ".js";
    if (snapshot || legacy) result.push_back(it->path());
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::string revision_name(const fs::path& path) {
  return fs::is_directory(path) ? path.filename().string() : path.stem().string();
}

std::optional<fs::path> revision_path(const fs::path& project_root,
                                      const std::string& name) {
  if (!valid_name(name) || name.front() != 'r') return std::nullopt;
  auto safe_root = revision_root_path(project_root);
  if (!safe_root) return std::nullopt;
  fs::path root = *safe_root;
  fs::path snapshot = root / name;
  fs::path legacy = root / (name + ".js");
  std::error_code ec;
  fs::file_status snapshot_status = fs::symlink_status(snapshot, ec);
  if (!ec && fs::is_directory(snapshot_status)) {
    fs::path candidate = fs::weakly_canonical(snapshot, ec);
    if (!ec && path_within(root, candidate)) return candidate;
  }
  ec.clear();
  fs::file_status legacy_status = fs::symlink_status(legacy, ec);
  if (!ec && fs::is_regular_file(legacy_status)) {
    fs::path candidate = fs::weakly_canonical(legacy, ec);
    if (!ec && path_within(root, candidate)) return candidate;
  }
  return std::nullopt;
}

std::optional<std::string> read_file_exact(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return std::nullopt;
  std::ostringstream output;
  output << input.rdbuf();
  if (!input.eof() && input.fail()) return std::nullopt;
  return output.str();
}

bool link_or_copy(const fs::path& source, const fs::path& destination) {
  std::error_code ec;
  fs::create_directories(destination.parent_path(), ec);
  if (ec) return false;
  fs::create_hard_link(source, destination, ec);
  if (!ec) return true;
  auto content = read_file_exact(source);
  return content && atomic_write(destination, *content);
}

bool copy_asset_tree(const fs::path& source, const fs::path& destination) {
  std::error_code ec;
  fs::create_directories(destination, ec);
  if (ec) return false;
  for (const fs::path& path : regular_files(source)) {
    fs::path relative = fs::relative(path, source, ec);
    if (ec || !link_or_copy(path, destination / relative)) return false;
  }
  return true;
}

bool same_asset_tree(const fs::path& left, const fs::path& right) {
  const std::vector<fs::path> left_files = regular_files(left);
  const std::vector<fs::path> right_files = regular_files(right);
  if (left_files.size() != right_files.size()) return false;
  std::error_code ec;
  for (size_t index = 0; index < left_files.size(); ++index) {
    if (fs::relative(left_files[index], left, ec) !=
        fs::relative(right_files[index], right, ec)) return false;
    auto left_content = read_file_exact(left_files[index]);
    auto right_content = read_file_exact(right_files[index]);
    if (!left_content || !right_content || *left_content != *right_content) return false;
  }
  return true;
}

bool snapshot_is_current(const fs::path& project_root, const fs::path& snapshot) {
  auto code = read_file_exact(project_root / "main.js");
  auto metadata = read_file_exact(project_root / "project.json");
  auto snapshot_code = read_file_exact(snapshot / "main.js");
  auto snapshot_metadata = read_file_exact(snapshot / "project.json");
  return code && metadata && snapshot_code && snapshot_metadata &&
         *code == *snapshot_code && *metadata == *snapshot_metadata &&
         same_asset_tree(project_root / "assets", snapshot / "assets");
}

bool save_revision(const fs::path& project_root) {
  auto code = read_file_exact(project_root / "main.js");
  auto metadata = read_file_exact(project_root / "project.json");
  if (!code || !metadata) return false;
  std::vector<fs::path> existing = revisions(project_root);
  unsigned next = 1;
  if (!existing.empty()) {
    std::string name = revision_name(existing.back());
    try { next = static_cast<unsigned>(std::stoul(name.substr(1))) + 1; }
    catch (...) { next = static_cast<unsigned>(existing.size()) + 1; }
  }
  std::ostringstream name;
  name << 'r' << std::setw(6) << std::setfill('0') << next;
  auto safe_root = revision_root_path(project_root);
  if (!safe_root) return false;
  fs::path revisions_root = *safe_root;
  fs::path temporary = revisions_root / (name.str() + ".new");
  fs::path destination = revisions_root / name.str();
  std::error_code ec;
  fs::remove_all(temporary, ec);
  if (!atomic_write(temporary / "main.js", *code) ||
      !atomic_write(temporary / "project.json", *metadata) ||
      !copy_asset_tree(project_root / "assets", temporary / "assets")) {
    fs::remove_all(temporary, ec);
    return false;
  }
  fs::rename(temporary, destination, ec);
  if (ec) {
    fs::remove_all(temporary, ec);
    return false;
  }
  existing = revisions(project_root);
  while (existing.size() > kRevisionLimit) {
    fs::remove_all(existing.front(), ec);
    existing.erase(existing.begin());
  }
  return true;
}

bool restore_snapshot(const fs::path& project_root, const fs::path& snapshot) {
  auto code = read_file_exact(snapshot / "main.js");
  auto metadata = read_file_exact(snapshot / "project.json");
  if (!code || !metadata) return false;
  fs::path work = project_root / "revisions" / ".restore.new";
  fs::path backup = project_root / "revisions" / ".restore.previous";
  std::error_code ec;
  fs::remove_all(work, ec);
  fs::remove_all(backup, ec);
  if (!atomic_write(work / "main.js", *code) ||
      !atomic_write(work / "project.json", *metadata) ||
      !copy_asset_tree(snapshot / "assets", work / "assets")) {
    fs::remove_all(work, ec);
    return false;
  }
  fs::create_directories(backup, ec);
  if (ec) return false;
  const fs::path names[] = {"main.js", "project.json", "assets"};
  size_t moved = 0;
  for (; moved < 3; ++moved) {
    fs::rename(project_root / names[moved], backup / names[moved], ec);
    if (ec) break;
  }
  const bool originals_moved = !ec && moved == 3;
  size_t installed = 0;
  if (originals_moved) {
    for (; installed < 3; ++installed) {
      fs::rename(work / names[installed], project_root / names[installed], ec);
      if (ec) break;
    }
  }
  if (ec) {
    for (size_t index = 0; index < installed; ++index)
      fs::remove_all(project_root / names[index], ec);
    for (size_t index = 0; index < moved; ++index)
      fs::rename(backup / names[index], project_root / names[index], ec);
    fs::remove_all(work, ec);
    fs::remove_all(backup, ec);
    return false;
  }
  fs::remove_all(work, ec);
  fs::remove_all(backup, ec);
  return true;
}

bool activate(const fs::path& apps_root, const std::string& name,
              const std::string& token, const fs::path& reload_signal) {
  std::error_code ec;
  fs::path project = apps_root / "projects" / name;
  if (!fs::is_directory(project, ec)) return false;
  fs::create_directories(apps_root, ec);
  if (ec) return false;
  const fs::path current = apps_root / "current";
  const fs::file_status current_status = fs::symlink_status(current, ec);
  const bool had_current = !ec && fs::is_symlink(current_status);
  const fs::path previous = had_current ? fs::read_symlink(current, ec) : fs::path{};
  if (had_current && ec) return false;
  ec.clear();
  fs::path temporary = apps_root / "current.new";
  fs::remove(temporary, ec);
  ec.clear();
  fs::create_symlink(project, temporary, ec);
  if (ec) return false;
  fs::rename(temporary, current, ec);
  if (ec) {
    fs::remove(temporary, ec);
    return false;
  }
  if (atomic_write(reload_signal, token + "\t" + name + "\n")) return true;

  // Publishing the selected project and asking the supervisor to reload it
  // form one operation. Restore the previous selection if the durable reload
  // request could not be written, so callers never observe a half-activation.
  if (had_current) {
    fs::path rollback = apps_root / "current.rollback";
    fs::remove(rollback, ec);
    ec.clear();
    fs::create_symlink(previous, rollback, ec);
    if (!ec) fs::rename(rollback, current, ec);
    if (ec) fs::remove(rollback, ec);
  } else {
    fs::remove(current, ec);
  }
  return false;
}

struct ReloadStatus {
  std::string token;
  std::string project;
  std::string state;
  std::string detail;
};

ReloadStatus read_reload_status(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  ReloadStatus status;
  if (!input) return status;
  std::getline(input, status.token, '\t');
  std::getline(input, status.project, '\t');
  std::getline(input, status.state, '\t');
  std::getline(input, status.detail);
  return status;
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
                              const fs::path& apps_root,
                              const fs::path& reload_signal,
                              const fs::path& console_log,
                              const fs::path& reload_status) {
  cJSON* request = length <= kRequestLimit ? cJSON_ParseWithLength(data, length) : nullptr;
  if (!request) {
    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "error");
    cJSON_AddBoolToObject(response, "ok", false);
    cJSON_AddStringToObject(response, "error",
                            length > kRequestLimit ? "request exceeds size limit" : "invalid JSON");
    cJSON_AddNumberToObject(response, "version", 2);
    return response;
  }

  cJSON* response = response_base(request);
  cJSON* type = cJSON_GetObjectItemCaseSensitive(request, "type");
  const char* command = cJSON_IsString(type) ? type->valuestring : "";
  std::string selected;
  auto project = project_path(apps_root, request, &selected);

  if (std::strcmp(command, "system.ping") == 0) {
    cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("system.pong"));
    cJSON_AddNumberToObject(response, "protocolVersion", 2);
    cJSON_AddStringToObject(response, "activeProject", active_name(apps_root).c_str());
    std::error_code ec;
    cJSON_AddBoolToObject(response, "persistenceReady",
                          fs::is_directory(apps_root / "projects", ec));
    const ReloadStatus status = read_reload_status(reload_status);
    cJSON_AddStringToObject(response, "rendererState", status.state.c_str());
    cJSON_AddStringToObject(response, "rendererProject", status.project.c_str());
  } else if (std::strcmp(command, "console.get") == 0) {
    cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("console"));
    cJSON* cursor_value = cJSON_GetObjectItemCaseSensitive(request, "cursor");
    uintmax_t cursor = cJSON_IsNumber(cursor_value) && cursor_value->valuedouble > 0
                       ? static_cast<uintmax_t>(cursor_value->valuedouble) : 0;
    std::error_code ec;
    uintmax_t size = fs::file_size(console_log, ec);
    if (ec) {
      cJSON_AddNumberToObject(response, "cursor", 0);
      cJSON_AddStringToObject(response, "content", "");
    } else {
      if (cursor > size) cursor = 0;
      if (size - cursor > kConsoleReadLimit) cursor = size - kConsoleReadLimit;
      std::ifstream input(console_log, std::ios::binary);
      input.seekg(static_cast<std::streamoff>(cursor));
      std::ostringstream content;
      content << input.rdbuf();
      cJSON_AddNumberToObject(response, "cursor", static_cast<double>(size));
      cJSON_AddStringToObject(response, "content", content.str().c_str());
    }
  } else if (std::strcmp(command, "project.status") == 0) {
    cJSON* token_value = cJSON_GetObjectItemCaseSensitive(request, "activation");
    std::string token = cJSON_IsString(token_value) ? token_value->valuestring : "";
    if (!valid_token(token)) {
      fail(response, "invalid activation token");
    } else {
      ReloadStatus status = read_reload_status(reload_status);
      cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("project.status"));
      cJSON_AddStringToObject(response, "activation", token.c_str());
      if (status.token == token) {
        cJSON_AddStringToObject(response, "project", status.project.c_str());
        cJSON_AddStringToObject(response, "state", status.state.c_str());
        cJSON_AddStringToObject(response, "detail", status.detail.c_str());
      } else {
        cJSON_AddStringToObject(response, "state", "pending");
      }
    }
  } else if (std::strcmp(command, "project.list") == 0) {
    cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("projects"));
    cJSON_AddStringToObject(response, "active", active_name(apps_root).c_str());
    cJSON* items = cJSON_AddArrayToObject(response, "projects");
    std::error_code ec;
    fs::path root = apps_root / "projects";
    std::vector<fs::path> paths;
    if (fs::exists(root, ec)) {
      for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        fs::file_status status = it->symlink_status(ec);
        if (!ec && fs::is_directory(status)) paths.push_back(it->path());
      }
    }
    std::sort(paths.begin(), paths.end());
    for (const fs::path& path : paths) {
      cJSON* item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "name", path.filename().string().c_str());
      std::string metadata = read_text(path / "project.json", "{}");
      cJSON* parsed = cJSON_Parse(metadata.c_str());
      cJSON_AddItemToObject(item, "metadata", parsed ? parsed : cJSON_CreateObject());
      cJSON_AddBoolToObject(item, "active", path.filename() == active_name(apps_root));
      cJSON_AddItemToArray(items, item);
    }
  } else if (std::strcmp(command, "project.create") == 0) {
    cJSON* name_value = cJSON_GetObjectItemCaseSensitive(request, "name");
    std::string name = cJSON_IsString(name_value) ? name_value->valuestring : "";
    cJSON* operation_value = cJSON_GetObjectItemCaseSensitive(request, "operation");
    std::string operation = cJSON_IsString(operation_value) ? operation_value->valuestring : "";
    if (!valid_name(name) || (!operation.empty() && !valid_token(operation))) {
      fail(response, "invalid project name");
    } else {
      fs::path destination = apps_root / "projects" / name;
      std::error_code ec;
      if (fs::exists(destination, ec)) {
        if (operation.empty() || read_text(destination / ".create-operation") != operation)
          fail(response, "project already exists");
        else
          cJSON_AddStringToObject(response, "project", name.c_str());
      } else {
        fs::create_directories(destination / "assets", ec);
        cJSON* metadata = cJSON_GetObjectItemCaseSensitive(request, "metadata");
        char* printed = cJSON_IsObject(metadata) ? cJSON_PrintUnformatted(metadata) : nullptr;
        bool ok = !ec && atomic_write(destination / "main.js", "// New microFX project\n") &&
                  atomic_write(destination / "project.json", printed ? printed : "{}") &&
                  (operation.empty() ||
                   atomic_write(destination / ".create-operation", operation));
        cJSON_free(printed);
        if (!ok) fail(response, "could not create project");
        else cJSON_AddStringToObject(response, "project", name.c_str());
      }
    }
  } else if (!project || !fs::is_directory(*project)) {
    fail(response, "invalid project");
  } else if (std::strcmp(command, "project.get") == 0) {
    cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("project"));
    cJSON_AddStringToObject(response, "project", selected.c_str());
    std::string code = read_text(*project / "main.js");
    cJSON_AddStringToObject(response, "code", code.c_str());
    std::string metadata = read_text(*project / "project.json", "{}");
    cJSON* parsed = cJSON_Parse(metadata.c_str());
    cJSON_AddItemToObject(response, "metadata", parsed ? parsed : cJSON_CreateObject());
    append_assets(response, *project);
    append_asset_folders(response, *project);
    cJSON* history = cJSON_AddArrayToObject(response, "revisions");
    for (const fs::path& path : revisions(*project))
      cJSON_AddItemToArray(history, cJSON_CreateString(revision_name(path).c_str()));
  } else if (std::strcmp(command, "project.save-run") == 0) {
    cJSON* content = cJSON_GetObjectItemCaseSensitive(request, "content");
    cJSON* requested_activation =
        cJSON_GetObjectItemCaseSensitive(request, "activation");
    std::string token = cJSON_IsString(requested_activation)
                            ? requested_activation->valuestring
                            : "";
    if (!cJSON_IsString(content) || std::strlen(content->valuestring) > kCodeLimit) {
      fail(response, "invalid project code");
    } else if (!valid_token(token)) {
      fail(response, "invalid activation token");
    } else {
      const std::string expected = token + "\t" + selected;
      const ReloadStatus status = read_reload_status(reload_status);
      const bool already_requested =
          (status.token == token && status.project == selected) ||
          read_text(reload_signal) == expected;
      bool accepted = already_requested;
      if (!accepted) {
        auto previous = read_file_exact(*project / "main.js");
        const bool changed = previous && *previous != content->valuestring;
        const bool saved = previous && (!changed || save_revision(*project)) &&
                           (!changed || atomic_write(*project / "main.js", content->valuestring));
        if (!saved) {
          fail(response, "could not write main.js");
        } else if (!activate(apps_root, selected, token, reload_signal)) {
          const bool restored = !changed || atomic_write(*project / "main.js", *previous);
          fail(response, restored
                             ? "could not activate project; main.js was restored"
                             : "could not activate project and could not restore main.js");
        } else {
          accepted = true;
        }
      }
      if (accepted) {
        cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("project.activation"));
        cJSON_AddStringToObject(response, "project", selected.c_str());
        cJSON_AddStringToObject(response, "activation", token.c_str());
        cJSON_AddStringToObject(
            response, "state",
            status.token == token && status.project == selected && !status.state.empty()
                ? status.state.c_str()
                : "requested");
      }
    }
  } else if (std::strcmp(command, "code.put") == 0) {
    cJSON* content = cJSON_GetObjectItemCaseSensitive(request, "content");
    auto previous = read_file_exact(*project / "main.js");
    if (!cJSON_IsString(content) || std::strlen(content->valuestring) > kCodeLimit ||
        (!previous || (*previous != content->valuestring && !save_revision(*project))) ||
        !atomic_write(*project / "main.js", cJSON_IsString(content) ? content->valuestring : ""))
      fail(response, "could not write main.js");
  } else if (std::strcmp(command, "project.metadata.put") == 0) {
    cJSON* metadata = cJSON_GetObjectItemCaseSensitive(request, "metadata");
    if (!cJSON_IsObject(metadata)) {
      fail(response, "metadata must be an object");
    } else {
      cJSON* title = cJSON_GetObjectItemCaseSensitive(metadata, "title");
      cJSON* description = cJSON_GetObjectItemCaseSensitive(metadata, "description");
      const size_t title_length = cJSON_IsString(title) ? std::strlen(title->valuestring) : 0;
      const size_t description_length = cJSON_IsString(description) ? std::strlen(description->valuestring) : 0;
      if (!cJSON_IsString(title) || title_length < 1 || title_length > 96 ||
          (description && (!cJSON_IsString(description) || description_length > 512))) {
        fail(response, "invalid project metadata");
      } else {
        char* printed = cJSON_PrintUnformatted(metadata);
        std::string previous = read_text(*project / "project.json", "{}");
        if (!printed || (previous != printed && !save_revision(*project)) ||
            !atomic_write(*project / "project.json", printed))
          fail(response, "could not write project metadata");
        cJSON_free(printed);
      }
    }
  } else if (std::strcmp(command, "revision.get") == 0) {
    cJSON* revision = cJSON_GetObjectItemCaseSensitive(request, "revision");
    std::string name = cJSON_IsString(revision) ? revision->valuestring : "";
    auto source = revision_path(*project, name);
    if (!source) {
      fail(response, "invalid revision");
    } else {
      const bool legacy = fs::is_regular_file(*source);
      fs::path snapshot = legacy ? source->parent_path().parent_path() : *source;
      std::string code = read_text(legacy ? *source : snapshot / "main.js");
      std::string metadata = legacy ? "{}" : read_text(snapshot / "project.json", "{}");
      cJSON* parsed = cJSON_Parse(metadata.c_str());
      cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("revision"));
      cJSON_AddStringToObject(response, "project", selected.c_str());
      cJSON_AddStringToObject(response, "revision", name.c_str());
      cJSON_AddBoolToObject(response, "legacy", legacy);
      cJSON_AddStringToObject(response, "code", code.c_str());
      cJSON_AddItemToObject(response, "metadata", parsed ? parsed : cJSON_CreateObject());
      if (legacy) {
        cJSON_AddArrayToObject(response, "assets");
        cJSON_AddArrayToObject(response, "folders");
      } else {
        append_assets(response, snapshot);
        append_asset_folders(response, snapshot);
      }
    }
  } else if (std::strcmp(command, "revision.restore") == 0) {
    cJSON* revision = cJSON_GetObjectItemCaseSensitive(request, "revision");
    std::string name = cJSON_IsString(revision) ? revision->valuestring : "";
    auto source = revision_path(*project, name);
    if (!source) {
      fail(response, "invalid revision");
    } else {
      if (fs::is_directory(*source)) {
        if (!snapshot_is_current(*project, *source) &&
            (!save_revision(*project) || !restore_snapshot(*project, *source)))
          fail(response, "could not restore revision");
      } else {
        std::string restored = read_text(*source);
        const std::string current = read_text(*project / "main.js");
        if (restored.empty() || (current != restored &&
            (!save_revision(*project) || !atomic_write(*project / "main.js", restored))))
          fail(response, "could not restore revision");
      }
    }
  } else if (std::strcmp(command, "asset.upload.status") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    cJSON* upload = cJSON_GetObjectItemCaseSensitive(request, "upload");
    auto size = requested_size(request, "size", kAssetLimit);
    auto destination = asset_path(*project, cJSON_IsString(path) ? path->valuestring : nullptr);
    std::string token = cJSON_IsString(upload) ? upload->valuestring : "";
    auto part = upload_path(*project, token, ".part");
    auto metadata = upload_path(*project, token, ".meta");
    if (!destination || !size || !part || !metadata) {
      fail(response, "invalid asset upload");
    } else {
      const std::string expected = upload_metadata(path->valuestring, *size);
      auto existing = read_file_exact(*metadata);
      std::error_code ec;
      if (!existing || *existing != expected) {
        fs::remove(*part, ec);
        if (!atomic_write(*metadata, expected) || !atomic_write(*part, "")) {
          fail(response, "could not initialize asset upload");
        }
      }
      uintmax_t offset = fs::file_size(*part, ec);
      if (ec) {
        fail(response, "could not inspect asset upload");
        offset = 0;
      }
      if (!ec && offset > *size) {
        fs::remove(*part, ec);
        if (!atomic_write(*part, "")) fail(response, "could not reset asset upload");
        offset = 0;
      }
      if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "ok"))) {
        cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("asset.upload"));
        cJSON_AddStringToObject(response, "upload", token.c_str());
        cJSON_AddNumberToObject(response, "offset", static_cast<double>(offset));
        cJSON_AddNumberToObject(response, "size", static_cast<double>(*size));
      }
    }
  } else if (std::strcmp(command, "asset.upload.chunk") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    cJSON* upload = cJSON_GetObjectItemCaseSensitive(request, "upload");
    cJSON* content = cJSON_GetObjectItemCaseSensitive(request, "content");
    auto size = requested_size(request, "size", kAssetLimit);
    auto offset = requested_size(request, "offset", kAssetLimit);
    auto destination = asset_path(*project, cJSON_IsString(path) ? path->valuestring : nullptr);
    std::string token = cJSON_IsString(upload) ? upload->valuestring : "";
    auto part = upload_path(*project, token, ".part");
    auto metadata = upload_path(*project, token, ".meta");
    auto decoded = cJSON_IsString(content) ? decode_base64(content->valuestring) : std::nullopt;
    auto stored_metadata = metadata ? read_file_exact(*metadata) : std::nullopt;
    if (!destination || !size || !offset || !part || !metadata || !decoded ||
        decoded->size() > kAssetChunkLimit || *offset + decoded->size() > *size ||
        !stored_metadata || *stored_metadata != upload_metadata(path->valuestring, *size)) {
      fail(response, "invalid asset upload chunk");
    } else {
      std::error_code ec;
      uintmax_t current = fs::file_size(*part, ec);
      bool written = false;
      if (!ec && current == *offset) {
        std::ofstream output(*part, std::ios::binary | std::ios::app);
        output.write(decoded->data(), static_cast<std::streamsize>(decoded->size()));
        written = static_cast<bool>(output);
        if (written) current += decoded->size();
      } else if (!ec && *offset + decoded->size() <= current) {
        std::ifstream input(*part, std::ios::binary);
        input.seekg(static_cast<std::streamoff>(*offset));
        std::string existing(decoded->size(), '\0');
        input.read(existing.data(), static_cast<std::streamsize>(existing.size()));
        written = static_cast<bool>(input) && existing == *decoded;
      }
      if (!written) {
        fail(response, "asset upload offset mismatch");
        cJSON_AddNumberToObject(response, "offset", static_cast<double>(ec ? 0 : current));
      } else {
        cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("asset.upload"));
        cJSON_AddStringToObject(response, "upload", token.c_str());
        cJSON_AddNumberToObject(response, "offset", static_cast<double>(current));
        cJSON_AddNumberToObject(response, "size", static_cast<double>(*size));
      }
    }
  } else if (std::strcmp(command, "asset.upload.commit") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    cJSON* upload = cJSON_GetObjectItemCaseSensitive(request, "upload");
    auto size = requested_size(request, "size", kAssetLimit);
    auto destination = asset_path(*project, cJSON_IsString(path) ? path->valuestring : nullptr);
    std::string token = cJSON_IsString(upload) ? upload->valuestring : "";
    auto part = upload_path(*project, token, ".part");
    auto metadata = upload_path(*project, token, ".meta");
    auto content = part ? read_file_exact(*part) : std::nullopt;
    auto stored_metadata = metadata ? read_file_exact(*metadata) : std::nullopt;
    if (!destination || !size || !part || !metadata || !content || content->size() != *size ||
        !stored_metadata || *stored_metadata != upload_metadata(path->valuestring, *size)) {
      fail(response, "incomplete asset upload");
    } else {
      auto previous = read_file_exact(*destination);
      if ((!previous || *previous != *content) && !save_revision(*project)) {
        fail(response, "could not save asset revision");
      } else {
        std::error_code ec;
        fs::create_directories(destination->parent_path(), ec);
        if (ec || !atomic_write(*destination, *content)) {
          fail(response, "could not commit asset upload");
        } else {
          fs::remove(*part, ec);
          fs::remove(*metadata, ec);
          cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("asset"));
          cJSON_AddStringToObject(response, "project", selected.c_str());
          cJSON_AddStringToObject(response, "path", path->valuestring);
          cJSON_AddNumberToObject(response, "size", static_cast<double>(*size));
        }
      }
    }
  } else if (std::strcmp(command, "asset.get.chunk") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    auto offset = requested_size(request, "offset", kAssetLimit);
    if (!offset) offset = 0;
    auto source = asset_path(*project, cJSON_IsString(path) ? path->valuestring : nullptr);
    std::error_code ec;
    uintmax_t size = source ? fs::file_size(*source, ec) : 0;
    if (!source || ec || size > kAssetLimit || *offset > size) {
      fail(response, "invalid asset chunk");
    } else {
      const size_t count = std::min(kAssetChunkLimit, static_cast<size_t>(size - *offset));
      std::ifstream input(*source, std::ios::binary);
      input.seekg(static_cast<std::streamoff>(*offset));
      std::string content(count, '\0');
      input.read(content.data(), static_cast<std::streamsize>(count));
      if (!input && static_cast<size_t>(input.gcount()) != count) {
        fail(response, "could not read asset chunk");
      } else {
        cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("asset.chunk"));
        cJSON_AddStringToObject(response, "project", selected.c_str());
        cJSON_AddStringToObject(response, "path", path->valuestring);
        cJSON_AddNumberToObject(response, "size", static_cast<double>(size));
        cJSON_AddNumberToObject(response, "offset", static_cast<double>(*offset));
        cJSON_AddNumberToObject(response, "next", static_cast<double>(*offset + count));
        cJSON_AddBoolToObject(response, "eof", *offset + count == size);
        cJSON_AddStringToObject(response, "encoding", "base64");
        cJSON_AddStringToObject(response, "content", encode_base64(content).c_str());
      }
    }
  } else if (std::strcmp(command, "asset.get") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    auto source = asset_path(*project, cJSON_IsString(path) ? path->valuestring : nullptr);
    if (!source) {
      fail(response, "invalid asset path");
    } else {
      std::ifstream input(*source, std::ios::binary);
      std::ostringstream bytes;
      if (input) bytes << input.rdbuf();
      if (!input || (!input.eof() && input.fail())) {
        fail(response, "could not read asset");
      } else {
        std::string content = bytes.str();
        std::string encoded = encode_base64(content);
        cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("asset"));
        cJSON_AddStringToObject(response, "project", selected.c_str());
        cJSON_AddStringToObject(response, "path", path->valuestring);
        cJSON_AddNumberToObject(response, "size", static_cast<double>(content.size()));
        cJSON_AddStringToObject(response, "encoding", "base64");
        cJSON_AddStringToObject(response, "content", encoded.c_str());
      }
    }
  } else if (std::strcmp(command, "asset.put") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    cJSON* content = cJSON_GetObjectItemCaseSensitive(request, "content");
    auto destination = asset_path(*project, cJSON_IsString(path) ? path->valuestring : nullptr);
    size_t encoded_length = cJSON_IsString(content) ? std::strlen(content->valuestring) : 0;
    if (!destination || !cJSON_IsString(content) ||
        encoded_length > ((kAssetLimit + 2u) / 3u) * 4u) {
      fail(response, "invalid asset");
    } else {
      auto decoded = decode_base64(content->valuestring);
      auto previous = destination ? read_file_exact(*destination) : std::nullopt;
      std::error_code ec;
      if (destination) fs::create_directories(destination->parent_path(), ec);
      if (!decoded || decoded->size() > kAssetLimit || ec ||
          ((!previous || *previous != *decoded) && !save_revision(*project)) ||
          !atomic_write(*destination, *decoded))
        fail(response, "could not write asset");
    }
  } else if (std::strcmp(command, "asset.folder.create") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    auto destination = asset_path(
        *project, cJSON_IsString(path) ? path->valuestring : nullptr);
    std::error_code ec;
    const fs::path asset_root = fs::weakly_canonical(*project / "assets", ec);
    if (!destination || ec || *destination == asset_root) {
      fail(response, "invalid asset folder");
    } else {
      fs::create_directories(*destination, ec);
      if (ec || !fs::is_directory(*destination, ec)) {
        fail(response, "could not create asset folder");
      } else {
        cJSON_ReplaceItemInObject(response, "type",
                                  cJSON_CreateString("asset.folder"));
        cJSON_AddStringToObject(response, "project", selected.c_str());
        cJSON_AddStringToObject(response, "path", path->valuestring);
      }
    }
  } else if (std::strcmp(command, "asset.delete") == 0) {
    cJSON* path = cJSON_GetObjectItemCaseSensitive(request, "path");
    auto destination = asset_path(*project, cJSON_IsString(path) ? path->valuestring : nullptr);
    std::error_code ec;
    if (!destination || (fs::exists(*destination, ec) && !save_revision(*project)) ||
        (!fs::remove(*destination, ec) && ec))
      fail(response, "could not delete asset");
  } else if (std::strcmp(command, "project.activate") == 0) {
    cJSON* id = cJSON_GetObjectItemCaseSensitive(request, "id");
    cJSON* requested_activation =
        cJSON_GetObjectItemCaseSensitive(request, "activation");
    std::string token = cJSON_IsString(requested_activation)
                            ? requested_activation->valuestring
                            : (cJSON_IsString(id) ? id->valuestring : "");
    if (!valid_token(token)) {
      fail(response, "invalid activation token");
    } else {
      const std::string expected = token + "\t" + selected;
      const ReloadStatus status = read_reload_status(reload_status);
      const bool already_requested =
          (status.token == token && status.project == selected) ||
          read_text(reload_signal) == expected;
      if (!already_requested && !activate(apps_root, selected, token, reload_signal)) {
        fail(response, "could not activate project");
      } else {
        cJSON_AddStringToObject(response, "activation", token.c_str());
        cJSON_AddStringToObject(
            response, "state",
            status.token == token && status.project == selected && !status.state.empty()
                ? status.state.c_str()
                : "requested");
      }
    }
  } else {
    fail(response, "unknown command");
  }

  cJSON_Delete(request);
  return response;
}

}  // namespace microfx
