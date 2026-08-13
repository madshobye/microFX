#include "project_protocol.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

cJSON* command(const fs::path& root, const fs::path& reload, const std::string& json) {
  cJSON* response = microfx::handle_project_command(json.data(), json.size(), root, reload);
  assert(response != nullptr);
  return response;
}

void require_ok(cJSON* response) {
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "ok")));
}

std::string string_value(cJSON* object, const char* name) {
  cJSON* value = cJSON_GetObjectItemCaseSensitive(object, name);
  assert(cJSON_IsString(value));
  return value->valuestring;
}

}  // namespace

int main() {
  fs::path sandbox = fs::temp_directory_path() / "microfx-project-protocol-test";
  fs::remove_all(sandbox);
  fs::path root = sandbox / "project";
  fs::path reload = sandbox / "run" / "reload";

  cJSON* response = command(root, reload,
      R"({"id":"put-code","type":"code.put","content":"fx.circle(1,2,3);\n"})");
  require_ok(response);
  assert(string_value(response, "id") == "put-code");
  cJSON_Delete(response);
  assert(microfx::read_text(root / "main.js") == "fx.circle(1,2,3);");

  // AP8A/w== is four bytes including NUL and 0xff, proving binary-safe I/O.
  response = command(root, reload,
      R"({"id":"put-asset","type":"asset.put","path":"models/test.bin","content":"AP8A/w=="})");
  require_ok(response);
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"id":"get-asset","type":"asset.get","path":"models/test.bin"})");
  require_ok(response);
  assert(string_value(response, "type") == "asset");
  assert(string_value(response, "encoding") == "base64");
  assert(string_value(response, "content") == "AP8A/w==");
  assert(cJSON_GetObjectItemCaseSensitive(response, "size")->valueint == 4);
  cJSON_Delete(response);

  response = command(root, reload, R"({"type":"project.get"})");
  require_ok(response);
  assert(string_value(response, "code") == "fx.circle(1,2,3);");
  cJSON* assets = cJSON_GetObjectItemCaseSensitive(response, "assets");
  assert(cJSON_GetArraySize(assets) == 1);
  assert(string_value(cJSON_GetArrayItem(assets, 0), "path") == "models/test.bin");
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"type":"asset.get","path":"../main.js"})");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  cJSON_Delete(response);

  response = command(root, reload, R"({"type":"project.activate"})");
  require_ok(response);
  cJSON_Delete(response);
  assert(microfx::read_text(reload) == "1");

  response = command(root, reload,
      R"({"type":"asset.delete","path":"models/test.bin"})");
  require_ok(response);
  cJSON_Delete(response);
  assert(!fs::exists(root / "assets/models/test.bin"));

  fs::remove_all(sandbox);
  std::cout << "project protocol tests passed\n";
  return 0;
}
