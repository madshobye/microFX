#include "project_protocol.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

cJSON* command(const fs::path& root, const fs::path& reload, const std::string& json) {
  cJSON* response = microfx::handle_project_command(
      json.data(), json.size(), root, reload, root.parent_path() / "canvas.log",
      root.parent_path() / "run" / "status");
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
  fs::path root = sandbox / "apps";
  fs::path reload = sandbox / "run" / "reload";
  fs::create_directories(root);
  std::ofstream(sandbox / "canvas.log") << "renderer ready\nframe 1\n";

  cJSON* response = command(root, reload,
      R"({"id":"ping-1","type":"system.ping"})");
  require_ok(response);
  assert(string_value(response, "type") == "system.pong");
  assert(string_value(response, "activeProject").empty());
  assert(cJSON_GetObjectItemCaseSensitive(response, "protocolVersion")->valueint == 2);
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "persistenceReady")));
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"id":"console-1","type":"console.get","cursor":0})");
  require_ok(response);
  assert(string_value(response, "type") == "console");
  assert(string_value(response, "content") == "renderer ready\nframe 1\n");
  assert(cJSON_GetObjectItemCaseSensitive(response, "cursor")->valueint == 23);
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"type":"project.create","name":"demo","operation":"project-create-1","metadata":{"title":"Demo"}})");
  require_ok(response);
  cJSON_Delete(response);

  // Losing a create acknowledgement and replaying the stable operation is
  // success, while an unrelated create request still cannot claim the name.
  response = command(root, reload,
      R"({"type":"project.create","name":"demo","operation":"project-create-1","metadata":{"title":"Demo"}})");
  require_ok(response);
  assert(string_value(response, "project") == "demo");
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"project.create","name":"demo","operation":"project-create-2","metadata":{"title":"Demo"}})");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"id":"activate-1","type":"project.activate","project":"demo"})");
  require_ok(response);
  assert(string_value(response, "activation") == "activate-1");
  cJSON_Delete(response);
  assert(microfx::read_text(reload) == "activate-1\tdemo");
  fs::remove(reload);

  response = command(root, reload,
      R"({"type":"project.status","activation":"activate-1"})");
  require_ok(response);
  assert(string_value(response, "state") == "pending");
  cJSON_Delete(response);
  fs::create_directories(sandbox / "run");
  std::ofstream(sandbox / "run" / "status")
      << "activate-1\tdemo\trunning\trenderer passed health check\n";
  response = command(root, reload,
      R"({"type":"project.status","activation":"activate-1"})");
  require_ok(response);
  assert(string_value(response, "state") == "running");
  assert(string_value(response, "project") == "demo");
  cJSON_Delete(response);

  // Replaying an explicit activation after losing its acknowledgement must
  // reuse the completed operation instead of restarting the renderer.
  assert(!fs::exists(reload));
  response = command(root, reload,
      R"({"id":"new-transport-request","type":"project.activate","project":"demo","activation":"activate-1"})");
  require_ok(response);
  assert(string_value(response, "activation") == "activate-1");
  assert(string_value(response, "state") == "running");
  cJSON_Delete(response);
  assert(!fs::exists(reload));

  // If the reload request cannot be published, Save & Run fails loudly while
  // leaving the new code and selection installed. It must not revive an older
  // application implicitly.
  response = command(root, reload,
      R"({"type":"project.create","name":"rollback-target","operation":"rollback-create","metadata":{"title":"Rollback target"}})");
  require_ok(response);
  cJSON_Delete(response);
  std::ofstream(sandbox / "not-a-directory") << "block reload path";
  const fs::path invalid_reload = sandbox / "not-a-directory" / "reload";
  const std::string failed_save_run =
      R"({"type":"project.save-run","project":"rollback-target","content":"FAIL_PARTIAL_WRITE","activation":"save-run-fail"})";
  response = microfx::handle_project_command(
      failed_save_run.data(), failed_save_run.size(), root, invalid_reload,
      sandbox / "canvas.log", sandbox / "run" / "status");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  assert(string_value(response, "error") ==
         "could not publish activation; new project remains installed");
  cJSON_Delete(response);
  assert(microfx::read_text(root / "projects/rollback-target/main.js") ==
         "FAIL_PARTIAL_WRITE");
  assert(fs::read_symlink(root / "current") == root / "projects/rollback-target");
  fs::remove_all(root / "projects/rollback-target");

  response = command(root, reload,
      R"({"type":"project.save-run","project":"demo","content":"fx.circle(8,9,10);","activation":"save-run-1"})");
  require_ok(response);
  assert(string_value(response, "type") == "project.activation");
  assert(string_value(response, "activation") == "save-run-1");
  cJSON_Delete(response);
  assert(microfx::read_text(root / "projects/demo/main.js") == "fx.circle(8,9,10);");
  assert(microfx::read_text(reload) == "save-run-1\tdemo");
  response = command(root, reload, R"({"type":"project.get","project":"demo"})");
  require_ok(response);
  const int revisions_before_replay = cJSON_GetArraySize(
      cJSON_GetObjectItemCaseSensitive(response, "revisions"));
  cJSON_Delete(response);
  fs::remove(reload);
  std::ofstream(sandbox / "run" / "status")
      << "save-run-1\tdemo\trunning\trenderer passed health check\n";
  response = command(root, reload,
      R"({"type":"project.save-run","project":"demo","content":"fx.circle(8,9,10);","activation":"save-run-1"})");
  require_ok(response);
  assert(string_value(response, "state") == "running");
  cJSON_Delete(response);
  assert(!fs::exists(reload));
  response = command(root, reload, R"({"type":"project.get","project":"demo"})");
  require_ok(response);
  assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(response, "revisions")) ==
         revisions_before_replay);
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"id":"put-code","type":"code.put","content":"const pixel=fx.image(\"assets/images/pixel.png\",10,20,32,32);\n"})");
  require_ok(response);
  assert(string_value(response, "id") == "put-code");
  cJSON_Delete(response);
  assert(microfx::read_text(root / "projects/demo/main.js") ==
         "const pixel=fx.image(\"assets/images/pixel.png\",10,20,32,32);");

  response = command(root, reload,
      R"({"type":"project.metadata.put","project":"demo","metadata":{"title":"Updated Demo","description":"Metadata round trip"}})");
  require_ok(response);
  cJSON_Delete(response);
  response = command(root, reload, R"({"type":"project.get","project":"demo"})");
  require_ok(response);
  cJSON* metadata = cJSON_GetObjectItemCaseSensitive(response, "metadata");
  assert(string_value(metadata, "title") == "Updated Demo");
  assert(string_value(metadata, "description") == "Metadata round trip");
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"type":"project.metadata.put","project":"demo","metadata":{"title":""}})");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  cJSON_Delete(response);

  // A valid 1x1 RGBA PNG proves image assets survive binary-safe transport.
  response = command(root, reload,
      R"({"id":"put-asset","type":"asset.put","path":"images/pixel.png","content":"iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="})");
  require_ok(response);
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"type":"asset.folder.create","project":"demo","path":"models/characters"})");
  require_ok(response);
  assert(string_value(response, "type") == "asset.folder");
  cJSON_Delete(response);
  assert(fs::is_directory(root / "projects/demo/assets/models/characters"));

  response = command(root, reload,
      R"({"id":"get-asset","type":"asset.get","path":"images/pixel.png"})");
  require_ok(response);
  assert(string_value(response, "type") == "asset");
  assert(string_value(response, "encoding") == "base64");
  assert(string_value(response, "content") ==
         "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  assert(cJSON_GetObjectItemCaseSensitive(response, "size")->valueint == 68);
  cJSON_Delete(response);

  // Chunked transfers stay well below data-channel message limits and can
  // resume after a disconnect by querying the same deterministic upload ID.
  response = command(root, reload,
      R"({"type":"asset.upload.status","project":"demo","path":"chunk.bin","upload":"resume-1","size":11})");
  require_ok(response);
  assert(cJSON_GetObjectItemCaseSensitive(response, "offset")->valueint == 0);
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"asset.upload.chunk","project":"demo","path":"chunk.bin","upload":"resume-1","size":11,"offset":0,"content":"aGVsbG8g"})");
  require_ok(response);
  assert(cJSON_GetObjectItemCaseSensitive(response, "offset")->valueint == 6);
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"asset.upload.status","project":"demo","path":"chunk.bin","upload":"resume-1","size":11})");
  require_ok(response);
  assert(cJSON_GetObjectItemCaseSensitive(response, "offset")->valueint == 6);
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"asset.upload.chunk","project":"demo","path":"chunk.bin","upload":"resume-1","size":11,"offset":0,"content":"aGVsbG8g"})");
  require_ok(response);
  assert(cJSON_GetObjectItemCaseSensitive(response, "offset")->valueint == 6);
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"asset.upload.chunk","project":"demo","path":"chunk.bin","upload":"resume-1","size":11,"offset":6,"content":"d29ybGQ="})");
  require_ok(response);
  assert(cJSON_GetObjectItemCaseSensitive(response, "offset")->valueint == 11);
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"asset.upload.commit","project":"demo","path":"chunk.bin","upload":"resume-1","size":11})");
  require_ok(response);
  cJSON_Delete(response);
  assert(microfx::read_text(root / "projects/demo/assets/chunk.bin") == "hello world");
  response = command(root, reload,
      R"({"type":"asset.get.chunk","project":"demo","path":"chunk.bin","offset":0})");
  require_ok(response);
  assert(string_value(response, "content") == "aGVsbG8gd29ybGQ=");
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "eof")));
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"asset.delete","project":"demo","path":"chunk.bin"})");
  require_ok(response);
  cJSON_Delete(response);

  response = command(root, reload, R"({"type":"project.get"})");
  require_ok(response);
  assert(string_value(response, "code") ==
         "const pixel=fx.image(\"assets/images/pixel.png\",10,20,32,32);");
  cJSON* assets = cJSON_GetObjectItemCaseSensitive(response, "assets");
  assert(cJSON_GetArraySize(assets) == 1);
  assert(string_value(cJSON_GetArrayItem(assets, 0), "path") == "images/pixel.png");
  cJSON* folders = cJSON_GetObjectItemCaseSensitive(response, "folders");
  assert(cJSON_IsArray(folders));
  assert(cJSON_GetArraySize(folders) >= 2);
  cJSON_Delete(response);

  response = command(root, reload, R"({"type":"project.list"})");
  require_ok(response);
  assert(string_value(response, "active") == "demo");
  assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(response, "projects")) == 1);
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"type":"code.put","content":"fx.rect(4,5,6,7);\n"})");
  require_ok(response);
  cJSON_Delete(response);

  response = command(root, reload, R"({"type":"project.get"})");
  require_ok(response);
  cJSON* revisions = cJSON_GetObjectItemCaseSensitive(response, "revisions");
  assert(cJSON_GetArraySize(revisions) >= 4);
  std::string revision =
      cJSON_GetArrayItem(revisions, cJSON_GetArraySize(revisions) - 1)->valuestring;
  cJSON_Delete(response);
  assert(fs::equivalent(root / "projects/demo/assets/images/pixel.png",
                        root / "projects/demo/revisions" / revision /
                            "assets/images/pixel.png"));

  response = command(root, reload,
      std::string("{\"type\":\"revision.get\",\"revision\":\"") + revision + "\"}");
  require_ok(response);
  assert(string_value(response, "type") == "revision");
  assert(string_value(response, "revision") == revision);
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "legacy")));
  assert(string_value(response, "code") ==
         "const pixel=fx.image(\"assets/images/pixel.png\",10,20,32,32);");
  metadata = cJSON_GetObjectItemCaseSensitive(response, "metadata");
  assert(string_value(metadata, "title") == "Updated Demo");
  assets = cJSON_GetObjectItemCaseSensitive(response, "assets");
  assert(cJSON_GetArraySize(assets) == 1);
  assert(string_value(cJSON_GetArrayItem(assets, 0), "path") == "images/pixel.png");
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"type":"project.metadata.put","project":"demo","metadata":{"title":"Temporary title"}})");
  require_ok(response);
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"asset.delete","path":"images/pixel.png"})");
  require_ok(response);
  cJSON_Delete(response);

  response = command(root, reload,
      std::string("{\"type\":\"revision.restore\",\"revision\":\"") + revision + "\"}");
  require_ok(response);
  cJSON_Delete(response);
  assert(microfx::read_text(root / "projects/demo/main.js") ==
         "const pixel=fx.image(\"assets/images/pixel.png\",10,20,32,32);");
  response = command(root, reload, R"({"type":"project.get","project":"demo"})");
  require_ok(response);
  metadata = cJSON_GetObjectItemCaseSensitive(response, "metadata");
  assert(string_value(metadata, "title") == "Updated Demo");
  assets = cJSON_GetObjectItemCaseSensitive(response, "assets");
  assert(cJSON_GetArraySize(assets) == 1);
  assert(string_value(cJSON_GetArrayItem(assets, 0), "path") == "images/pixel.png");
  const int revision_count_after_restore = cJSON_GetArraySize(
      cJSON_GetObjectItemCaseSensitive(response, "revisions"));
  cJSON_Delete(response);

  // A lost restore acknowledgement may replay the command. The current state
  // already equals the snapshot, so the replay must not add another revision.
  response = command(root, reload,
      std::string("{\"type\":\"revision.restore\",\"revision\":\"") + revision + "\"}");
  require_ok(response);
  cJSON_Delete(response);
  response = command(root, reload, R"({"type":"project.get","project":"demo"})");
  require_ok(response);
  assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(response, "revisions")) ==
         revision_count_after_restore);
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"asset.get","path":"images/pixel.png"})");
  require_ok(response);
  assert(string_value(response, "content") ==
         "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=");
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"type":"asset.get","path":"../main.js"})");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  cJSON_Delete(response);

  // Lexical path checks are insufficient: project-controlled symlinks must not
  // expose files outside the project through get, put, list, or delete.
  std::ofstream(sandbox / "outside-secret") << "not an asset";
  fs::create_symlink(sandbox / "outside-secret",
                     root / "projects/demo/assets/escape.bin");
  response = command(root, reload,
      R"({"type":"asset.get","path":"escape.bin"})");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"asset.put","path":"escape.bin","content":"cHduZWQ="})");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  cJSON_Delete(response);
  assert(microfx::read_text(sandbox / "outside-secret") == "not an asset");
  response = command(root, reload, R"({"type":"project.get","project":"demo"})");
  require_ok(response);
  assets = cJSON_GetObjectItemCaseSensitive(response, "assets");
  assert(cJSON_GetArraySize(assets) == 1);
  assert(string_value(cJSON_GetArrayItem(assets, 0), "path") == "images/pixel.png");
  cJSON_Delete(response);

  fs::create_directory_symlink(sandbox, root / "projects/escaped-project");
  response = command(root, reload,
      R"({"type":"project.get","project":"escaped-project"})");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  cJSON_Delete(response);
  response = command(root, reload, R"({"type":"project.list"})");
  require_ok(response);
  assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(response, "projects")) == 1);
  cJSON_Delete(response);

  response = command(root, reload,
      R"({"id":"activate-2","type":"project.activate"})");
  require_ok(response);
  cJSON_Delete(response);
  assert(microfx::read_text(reload) == "activate-2\tdemo");

  response = command(root, reload,
      R"({"type":"asset.delete","path":"images/pixel.png"})");
  require_ok(response);
  cJSON_Delete(response);
  assert(!fs::exists(root / "projects/demo/assets/images/pixel.png"));

  // Code-only revisions made by protocol v2 remain usable after the v3
  // whole-project snapshot upgrade.
  std::ofstream(root / "projects/demo/revisions/r900000.js") << "fx.circle(9,9,9);\n";
  response = command(root, reload,
      R"({"type":"revision.get","revision":"r900000"})");
  require_ok(response);
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "legacy")));
  assert(string_value(response, "code") == "fx.circle(9,9,9);");
  assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(response, "assets")) == 0);
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"revision.restore","revision":"r900000"})");
  require_ok(response);
  cJSON_Delete(response);
  assert(microfx::read_text(root / "projects/demo/main.js") == "fx.circle(9,9,9);");

  // The revision root itself is project-controlled and must not be able to
  // redirect inspection or restoration outside the selected project.
  fs::create_directories(sandbox / "outside-revisions/r777777");
  std::ofstream(sandbox / "outside-revisions/r777777/main.js") << "secret";
  fs::rename(root / "projects/demo/revisions", root / "projects/demo/revisions.saved");
  fs::create_directory_symlink(sandbox / "outside-revisions",
                               root / "projects/demo/revisions");
  response = command(root, reload,
      R"({"type":"revision.get","revision":"r777777"})");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  cJSON_Delete(response);
  response = command(root, reload,
      R"({"type":"revision.restore","revision":"r777777"})");
  assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(response, "ok")));
  cJSON_Delete(response);

  fs::remove_all(sandbox);
  std::cout << "project protocol tests passed\n";
  return 0;
}
