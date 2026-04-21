#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "hyprmacs/plugin_state.hpp"

namespace hyprmacs {

struct ProtocolMessage {
    int version = 0;
    std::string type;
    WorkspaceId workspace_id;
    Timestamp timestamp;
    std::string payload_json;
};

std::optional<ProtocolMessage> parse_message(std::string_view json);
std::string serialize_message(const ProtocolMessage& message);

std::optional<StateDumpPayload> parse_state_dump_payload(std::string_view payload_json);
std::string serialize_state_dump_payload(const StateDumpPayload& payload);

}  // namespace hyprmacs
