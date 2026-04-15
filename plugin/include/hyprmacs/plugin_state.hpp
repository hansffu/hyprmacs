#pragma once

#include <optional>
#include <string>
#include <vector>

namespace hyprmacs {

using WorkspaceId = std::string;
using ClientId = std::string;
using BufferId = std::string;
using Timestamp = std::string;

enum class InputMode {
    kUnknown,
    kEmacsControl,
    kClientControl,
};

struct ClientSnapshot {
    ClientId client_id;
    std::string title;
    std::string app_id;
    bool floating = false;
};

struct StateDumpPayload {
    bool managed = false;
    bool controller_connected = false;
    std::vector<ClientSnapshot> eligible_clients;
    std::vector<ClientId> managed_clients;
    std::optional<ClientId> selected_client;
    std::optional<InputMode> input_mode;
};

}  // namespace hyprmacs
