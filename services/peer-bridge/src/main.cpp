#include <libwebsockets.h>
#include "project_protocol.h"

extern "C" {
#include <cjson/cJSON.h>
#include <peer.h>
}

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr const char* kProjectRoot = "/data/apps/current";
constexpr const char* kPeerIdPath = "/data/config/peer-id";
constexpr const char* kReloadSignal = "/run/microfx-project-reload";
constexpr const char* kPeerHost = "0.peerjs.com";
constexpr int kPeerPort = 443;

struct Bridge {
  lws_context* context = nullptr;
  lws* socket = nullptr;
  PeerConnection* peer = nullptr;
  // libpeer may invoke ICE callbacks synchronously while an SDP operation is
  // in progress, so callbacks must be allowed to enqueue signaling recursively.
  std::recursive_mutex mutex;
  std::queue<std::string> outbound;
  std::atomic<bool> running{true};
  std::string peer_id;
  std::string remote_id;
  std::string connection_id;
  bool signaling_open = false;
  unsigned negotiation = 0;
};

Bridge g;

std::string json_print(cJSON* value) {
  char* printed = cJSON_PrintUnformatted(value);
  std::string result = printed ? printed : "{}";
  cJSON_free(printed);
  return result;
}

std::string application_mid(const std::string& sdp) {
  const size_t media = sdp.find("m=application ");
  if (media == std::string::npos) return {};
  const size_t next_media = sdp.find("\r\nm=", media + 1);
  const size_t mid = sdp.find("a=mid:", media);
  if (mid == std::string::npos || (next_media != std::string::npos && mid > next_media)) return {};
  const size_t value = mid + std::strlen("a=mid:");
  const size_t end = sdp.find("\r\n", value);
  if (end == std::string::npos || end == value) return {};
  return sdp.substr(value, end - value);
}

std::string sdp_shape(const std::string& sdp) {
  std::ostringstream out;
  size_t media = sdp.find("m=");
  bool first = true;
  while (media != std::string::npos) {
    const size_t line_end = sdp.find("\r\n", media);
    const size_t section_end = sdp.find("\r\nm=", media + 1);
    const size_t mid = sdp.find("a=mid:", media);
    if (!first) out << ", ";
    first = false;
    out << sdp.substr(media, (line_end == std::string::npos ? sdp.size() : line_end) - media);
    out << " mid=";
    if (mid != std::string::npos && (section_end == std::string::npos || mid < section_end)) {
      const size_t value = mid + std::strlen("a=mid:");
      const size_t end = sdp.find("\r\n", value);
      out << sdp.substr(value, (end == std::string::npos ? sdp.size() : end) - value);
    } else {
      out << "<none>";
    }
    media = section_end == std::string::npos ? std::string::npos : section_end + 2;
  }
  return out.str();
}

std::string candidate_shape(const char* candidate) {
  std::istringstream input(candidate ? candidate : "");
  std::vector<std::string> fields;
  std::string field;
  while (input >> field) fields.push_back(field);
  std::ostringstream out;
  if (fields.size() >= 8) {
    out << "protocol=" << fields[2] << " endpoint=" << fields[4] << ':' << fields[5];
    for (size_t i = 6; i + 1 < fields.size(); ++i) {
      if (fields[i] == "typ") { out << " type=" << fields[i + 1]; break; }
    }
  } else {
    out << "malformed";
  }
  return out.str();
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
  if (from.empty()) return;
  size_t position = 0;
  while ((position = text.find(from, position)) != std::string::npos) {
    text.replace(position, from.size(), to);
    position += to.size();
  }
}

std::string conform_answer_to_offer(const char* answer, const char* offer) {
  std::string result = answer ? answer : "";
  const std::string offered_mid = application_mid(offer ? offer : "");
  if (!offered_mid.empty() && offered_mid != "datachannel") {
    replace_all(result, "a=group:BUNDLE datachannel", "a=group:BUNDLE " + offered_mid);
    replace_all(result, "a=mid:datachannel", "a=mid:" + offered_mid);
  }
  return result;
}

void queue_signal(std::string message) {
  std::lock_guard lock(g.mutex);
  g.outbound.push(std::move(message));
  if (g.context) lws_cancel_service(g.context);
}

void send_peer_json(cJSON* value, uint16_t sid) {
  std::string output = json_print(value);
  cJSON_Delete(value);
  std::lock_guard lock(g.mutex);
  if (g.peer) {
    const int result = peer_connection_datachannel_send_sid(g.peer, output.data(), output.size(), sid);
    std::fprintf(stderr, "NEGOTIATION %u DEVICE data-out sid=%u bytes=%zu result=%d\n",
                 g.negotiation, sid, output.size(), result);
  }
}

void on_data(char* message, size_t length, void*, uint16_t sid) {
  std::fprintf(stderr, "NEGOTIATION %u DEVICE data-in sid=%u bytes=%zu\n",
               g.negotiation, sid, length);
  send_peer_json(microfx::handle_project_command(message, length, kProjectRoot, kReloadSignal), sid);
}

void on_open(void*) { std::fprintf(stderr, "peer data channel open\n"); }
void on_close(void*) { std::fprintf(stderr, "peer data channel closed\n"); }

void on_state(PeerConnectionState state, void*) {
  std::fprintf(stderr, "NEGOTIATION %u DEVICE ice-state %s\n", g.negotiation,
               peer_connection_state_to_string(state));
}

void send_candidate(char* candidate, void*) {
  if (!candidate || std::strncmp(candidate, "candidate:", 10) != 0) return;
  std::fprintf(stderr, "NEGOTIATION %u DEVICE candidate-out %s\n", g.negotiation,
               candidate_shape(candidate).c_str());
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "CANDIDATE");
  cJSON_AddStringToObject(root, "dst", g.remote_id.c_str());
  cJSON* payload = cJSON_AddObjectToObject(root, "payload");
  cJSON* body = cJSON_AddObjectToObject(payload, "candidate");
  cJSON_AddStringToObject(body, "candidate", candidate);
  cJSON_AddStringToObject(body, "sdpMid", "0");
  cJSON_AddNumberToObject(body, "sdpMLineIndex", 0);
  cJSON_AddStringToObject(payload, "type", "data");
  cJSON_AddStringToObject(payload, "connectionId", g.connection_id.c_str());
  queue_signal(json_print(root));
  cJSON_Delete(root);
}

void destroy_peer_locked() {
  if (!g.peer) return;
  peer_connection_destroy(g.peer);
  g.peer = nullptr;
}

bool create_answer(cJSON* envelope) {
  cJSON* source = cJSON_GetObjectItemCaseSensitive(envelope, "src");
  cJSON* payload = cJSON_GetObjectItemCaseSensitive(envelope, "payload");
  cJSON* connection = cJSON_GetObjectItemCaseSensitive(payload, "connectionId");
  cJSON* sdp_object = cJSON_GetObjectItemCaseSensitive(payload, "sdp");
  cJSON* sdp = cJSON_GetObjectItemCaseSensitive(sdp_object, "sdp");
  if (!cJSON_IsString(source) || !cJSON_IsString(connection) || !cJSON_IsString(sdp)) return false;

  std::lock_guard lock(g.mutex);
  ++g.negotiation;
  std::fprintf(stderr, "NEGOTIATION %u DEVICE begin connection=%s\n", g.negotiation,
               connection->valuestring);
  std::fprintf(stderr, "NEGOTIATION %u DEVICE offer-shape %s\n", g.negotiation,
               sdp_shape(sdp->valuestring).c_str());
  destroy_peer_locked();
  g.remote_id = source->valuestring;
  g.connection_id = connection->valuestring;
  PeerConfiguration config{};
  config.ice_servers[0].urls = "stun:stun.l.google.com:19302";
  config.datachannel = DATA_CHANNEL_STRING;
  g.peer = peer_connection_create(&config);
  if (!g.peer) return false;
  peer_connection_oniceconnectionstatechange(g.peer, on_state);
  peer_connection_onicecandidate(g.peer, send_candidate);
  peer_connection_ondatachannel(g.peer, on_data, on_open, on_close);
  peer_connection_set_remote_description(g.peer, sdp->valuestring, SDP_TYPE_OFFER);
  const char* answer = peer_connection_create_answer(g.peer);
  if (!answer) return false;
  const std::string conformed_answer = conform_answer_to_offer(answer, sdp->valuestring);
  std::fprintf(stderr, "NEGOTIATION %u DEVICE answer-shape %s\n", g.negotiation,
               sdp_shape(conformed_answer).c_str());

  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ANSWER");
  cJSON_AddStringToObject(root, "dst", g.remote_id.c_str());
  cJSON* answer_payload = cJSON_AddObjectToObject(root, "payload");
  cJSON* answer_sdp = cJSON_AddObjectToObject(answer_payload, "sdp");
  cJSON_AddStringToObject(answer_sdp, "type", "answer");
  cJSON_AddStringToObject(answer_sdp, "sdp", conformed_answer.c_str());
  cJSON_AddStringToObject(answer_payload, "type", "data");
  cJSON_AddStringToObject(answer_payload, "connectionId", g.connection_id.c_str());
  g.outbound.push(json_print(root));
  cJSON_Delete(root);
  if (g.socket) lws_callback_on_writable(g.socket);
  return true;
}

void add_candidate(cJSON* envelope) {
  cJSON* payload = cJSON_GetObjectItemCaseSensitive(envelope, "payload");
  cJSON* body = cJSON_GetObjectItemCaseSensitive(payload, "candidate");
  cJSON* candidate = cJSON_GetObjectItemCaseSensitive(body, "candidate");
  if (!cJSON_IsString(candidate)) return;
  const bool tcp = std::strstr(candidate->valuestring, " tcp ") != nullptr;
  std::fprintf(stderr, "NEGOTIATION %u DEVICE candidate-in %s%s\n", g.negotiation,
               candidate_shape(candidate->valuestring).c_str(), tcp ? " dropped=tcp" : "");
  if (tcp) return;
  std::lock_guard lock(g.mutex);
  if (g.peer) peer_connection_add_ice_candidate(g.peer, candidate->valuestring);
}

void handle_signal(const char* data, size_t length) {
  cJSON* root = cJSON_ParseWithLength(data, length);
  if (!root) return;
  cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
  const char* name = cJSON_IsString(type) ? type->valuestring : "";
  if (std::strcmp(name, "OPEN") == 0) {
    g.signaling_open = true;
    std::fprintf(stderr, "PeerJS ready: %s\n", g.peer_id.c_str());
  } else if (std::strcmp(name, "OFFER") == 0) {
    if (!create_answer(root)) std::fprintf(stderr, "invalid PeerJS offer\n");
  } else if (std::strcmp(name, "CANDIDATE") == 0) {
    add_candidate(root);
  } else if (std::strcmp(name, "ID-TAKEN") == 0) {
    std::fprintf(stderr, "PeerJS id already in use: %s\n", g.peer_id.c_str());
  } else if (std::strcmp(name, "LEAVE") == 0) {
    std::lock_guard lock(g.mutex);
    destroy_peer_locked();
  }
  cJSON_Delete(root);
}

int websocket_callback(lws* socket, lws_callback_reasons reason, void*, void* input, size_t length) {
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      g.socket = socket;
      std::fprintf(stderr, "PeerJS websocket connected\n");
      break;
    case LWS_CALLBACK_CLIENT_RECEIVE:
      handle_signal(static_cast<const char*>(input), length);
      break;
    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      std::string message;
      {
        std::lock_guard lock(g.mutex);
        if (g.outbound.empty()) break;
        message = std::move(g.outbound.front());
        g.outbound.pop();
      }
      std::vector<unsigned char> buffer(LWS_PRE + message.size());
      std::memcpy(buffer.data() + LWS_PRE, message.data(), message.size());
      lws_write(socket, buffer.data() + LWS_PRE, message.size(), LWS_WRITE_TEXT);
      std::lock_guard lock(g.mutex);
      if (!g.outbound.empty()) lws_callback_on_writable(socket);
      break;
    }
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
      if (g.socket) lws_callback_on_writable(g.socket);
      break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      std::fprintf(stderr, "PeerJS websocket error: %s\n", input ? static_cast<const char*>(input) : "unknown");
      g.socket = nullptr;
      g.signaling_open = false;
      g.running = false;
      break;
    case LWS_CALLBACK_CLIENT_CLOSED:
      g.socket = nullptr;
      g.signaling_open = false;
      g.running = false;
      std::fprintf(stderr, "PeerJS websocket closed\n");
      break;
    default:
      break;
  }
  return 0;
}

std::string token() {
  std::mt19937_64 random(std::random_device{}());
  std::ostringstream out;
  out << std::hex << random();
  return out.str();
}
}  // namespace

int main() {
  g.peer_id = microfx::read_text(kPeerIdPath, "microfx-demo");
  if (peer_init() != 0) {
    std::fprintf(stderr, "libpeer initialization failed\n");
    return 1;
  }

  lws_protocols protocols[] = {
      {"microfx-peerjs", websocket_callback, 0, 64 * 1024, 0, nullptr, 0},
      {nullptr, nullptr, 0, 0, 0, nullptr, 0}};
  lws_context_creation_info context_info{};
  context_info.port = CONTEXT_PORT_NO_LISTEN;
  context_info.protocols = protocols;
  context_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  context_info.client_ssl_ca_filepath = "/etc/ssl/certs/ca-certificates.crt";
  g.context = lws_create_context(&context_info);
  if (!g.context) return 1;

  std::string path = "/peerjs?key=peerjs&id=" + g.peer_id + "&token=" + token() + "&version=1.5.5";
  lws_client_connect_info connection{};
  connection.context = g.context;
  connection.address = kPeerHost;
  connection.port = kPeerPort;
  connection.path = path.c_str();
  connection.host = kPeerHost;
  connection.origin = kPeerHost;
  connection.ssl_connection = LCCSCF_USE_SSL;
  connection.local_protocol_name = protocols[0].name;
  if (!lws_client_connect_via_info(&connection)) {
    std::fprintf(stderr, "could not start PeerJS websocket\n");
    return 1;
  }

  std::thread peer_loop([] {
    while (g.running) {
      {
        std::lock_guard lock(g.mutex);
        if (g.peer) peer_connection_loop(g.peer);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  auto last_heartbeat = std::chrono::steady_clock::now();
  auto last_config_check = last_heartbeat;
  while (g.running && lws_service(g.context, 50) >= 0) {
    auto now = std::chrono::steady_clock::now();
    if (g.signaling_open && now - last_heartbeat >= std::chrono::seconds(5)) {
      queue_signal("{\"type\":\"HEARTBEAT\"}");
      last_heartbeat = now;
    }
    if (now - last_config_check >= std::chrono::seconds(2)) {
      if (microfx::read_text(kPeerIdPath, "microfx-demo") != g.peer_id) {
        std::fprintf(stderr, "Peer ID changed; restarting bridge\n");
        g.running = false;
      }
      last_config_check = now;
    }
  }

  g.running = false;
  peer_loop.join();
  {
    std::lock_guard lock(g.mutex);
    destroy_peer_locked();
  }
  lws_context_destroy(g.context);
  peer_deinit();
  return 0;
}
