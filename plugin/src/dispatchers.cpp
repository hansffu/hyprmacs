#include "hyprmacs/dispatchers.hpp"

#include <cctype>

namespace hyprmacs {
namespace {

std::string trim_copy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(start, end - start);
}

}  // namespace

DispatcherOutcome dispatch_set_emacs_control_mode(
    const std::string& arg,
    WorkspaceManager& workspace_manager,
    FocusController& focus_controller,
    ActiveWorkspaceResolver active_workspace_resolver
) {
    DispatcherOutcome out;
    const std::string trimmed_arg = trim_copy(arg);
    if (!trimmed_arg.empty()) {
        out.workspace_id = trimmed_arg;
    } else if (active_workspace_resolver) {
        out.workspace_id = active_workspace_resolver();
    }

    if (!out.workspace_id.has_value()) {
        out.workspace_id = workspace_manager.managed_workspace();
    }
    if (!out.workspace_id.has_value()) {
        out.error = "no target workspace resolved";
        return out;
    }

    if (!workspace_manager.set_input_mode(*out.workspace_id, InputMode::kEmacsControl)) {
        out.error = "target workspace is not managed";
        return out;
    }

    const auto emacs_client = workspace_manager.emacs_client(*out.workspace_id);
    if (emacs_client.has_value() && !focus_controller.focus_client(*emacs_client)) {
        out.error = "failed to focus managing emacs client";
        return out;
    }

    out.success = true;
    return out;
}

}  // namespace hyprmacs
