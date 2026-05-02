#pragma once

#include <functional>
#include <optional>
#include <string>

#include "hyprmacs/workspace_manager.hpp"

namespace hyprmacs {

struct DispatcherOutcome {
    bool success = false;
    std::optional<WorkspaceId> workspace_id;
    std::optional<ClientId> focus_client_id;
    std::string error;
};

using ActiveWorkspaceResolver = std::function<std::optional<WorkspaceId>()>;

DispatcherOutcome dispatch_set_emacs_control_mode(
    const std::string& arg,
    WorkspaceManager& workspace_manager,
    ActiveWorkspaceResolver active_workspace_resolver = {}
);

DispatcherOutcome dispatch_manage_active_window(
    const std::string& arg,
    WorkspaceManager& workspace_manager,
    ActiveWorkspaceResolver active_workspace_resolver = {}
);

}  // namespace hyprmacs
