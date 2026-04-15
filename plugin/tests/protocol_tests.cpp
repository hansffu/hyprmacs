#include "hyprmacs/protocol.hpp"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool test_parse_manage_workspace() {
    const std::string fixture = R"json(
{
  "version": 1,
  "type": "manage-workspace",
  "workspace_id": "1",
  "timestamp": "2026-04-15T12:00:00Z",
  "payload": {
    "adopt_existing_clients": true
  }
}
)json";

    const auto message = hyprmacs::parse_message(fixture);
    if (!expect(message.has_value(), "parse_message should parse manage-workspace fixture")) {
        return false;
    }

    bool ok = true;
    ok &= expect(message->version == 1, "version should be 1");
    ok &= expect(message->type == "manage-workspace", "type should be manage-workspace");
    ok &= expect(message->workspace_id == "1", "workspace_id should be 1");
    ok &= expect(message->timestamp == "2026-04-15T12:00:00Z", "timestamp should match");
    ok &= expect(message->payload_json.find("\"adopt_existing_clients\": true") != std::string::npos,
                 "payload should contain adopt_existing_clients true");
    return ok;
}

bool test_parse_and_serialize_state_dump() {
    const std::string state_dump_message = R"json(
{
  "version": 1,
  "type": "state-dump",
  "workspace_id": "1",
  "timestamp": "2026-04-15T12:00:00Z",
  "payload": {
    "managed": true,
    "controller_connected": true,
    "eligible_clients": [
      {
        "client_id": "0xabc123",
        "title": "foot",
        "app_id": "foot",
        "floating": false
      }
    ],
    "managed_clients": [],
    "selected_client": null,
    "input_mode": null
  }
}
)json";

    const auto message = hyprmacs::parse_message(state_dump_message);
    if (!expect(message.has_value(), "parse_message should parse state-dump fixture")) {
        return false;
    }

    const auto state_dump = hyprmacs::parse_state_dump_payload(message->payload_json);
    if (!expect(state_dump.has_value(), "parse_state_dump_payload should parse payload")) {
        return false;
    }

    bool ok = true;
    ok &= expect(state_dump->managed, "managed should be true");
    ok &= expect(state_dump->controller_connected, "controller_connected should be true");
    ok &= expect(state_dump->eligible_clients.size() == 1, "eligible_clients size should be 1");
    ok &= expect(state_dump->eligible_clients[0].client_id == "0xabc123", "client_id should match");
    ok &= expect(state_dump->eligible_clients[0].title == "foot", "title should match");
    ok &= expect(state_dump->eligible_clients[0].app_id == "foot", "app_id should match");
    ok &= expect(!state_dump->eligible_clients[0].floating, "floating should be false");
    ok &= expect(state_dump->managed_clients.empty(), "managed_clients should be empty");
    ok &= expect(!state_dump->selected_client.has_value(), "selected_client should be null");
    ok &= expect(!state_dump->input_mode.has_value(), "input_mode should be null");

    const auto serialized_payload = hyprmacs::serialize_state_dump_payload(*state_dump);
    const auto reparsed_payload = hyprmacs::parse_state_dump_payload(serialized_payload);
    ok &= expect(reparsed_payload.has_value(), "serialized payload should be parseable");
    ok &= expect(reparsed_payload->eligible_clients.size() == 1, "reparsed eligible_clients size should be 1");

    const hyprmacs::ProtocolMessage to_serialize {
        .version = 1,
        .type = "state-dump",
        .workspace_id = "1",
        .timestamp = "2026-04-15T12:00:00Z",
        .payload_json = serialized_payload,
    };
    const auto serialized_message = hyprmacs::serialize_message(to_serialize);
    const auto reparsed_message = hyprmacs::parse_message(serialized_message);
    ok &= expect(reparsed_message.has_value(), "serialized message should be parseable");
    ok &= expect(reparsed_message->type == "state-dump", "reparsed type should be state-dump");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_parse_manage_workspace();
    ok &= test_parse_and_serialize_state_dump();
    return ok ? 0 : 1;
}
