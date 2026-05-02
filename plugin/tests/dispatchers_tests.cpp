#include "hyprmacs/dispatchers.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "hyprmacs/workspace_manager.hpp"

namespace {

bool expect(bool cond, const char* message) {
    if (!cond) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return cond;
}

bool test_dispatcher_sets_emacs_control_and_reports_emacs_focus_target() {
    hyprmacs::WorkspaceManager manager;

    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-primary");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    manager.manage_workspace("1");
    manager.set_input_mode("1", hyprmacs::InputMode::kClientControl);

    const auto outcome =
        hyprmacs::dispatch_set_emacs_control_mode("", manager, []() -> std::optional<std::string> {
            return "1";
        });

    bool ok = true;
    ok &= expect(outcome.success, "dispatcher should succeed for managed workspace");
    ok &= expect(outcome.workspace_id.has_value() && *outcome.workspace_id == "1",
                 "dispatcher should report workspace 1");
    ok &= expect(outcome.focus_client_id.has_value() && *outcome.focus_client_id == "0xeee",
                 "dispatcher should report managing emacs client focus target");

    const auto state = manager.build_state_dump("1");
    ok &= expect(state.input_mode.has_value() && *state.input_mode == hyprmacs::InputMode::kEmacsControl,
                 "dispatcher should set input mode to emacs-control");
    return ok;
}

bool test_dispatcher_fails_for_unmanaged_workspace_argument() {
    hyprmacs::WorkspaceManager manager;
    manager.manage_workspace("1");

    const auto outcome =
        hyprmacs::dispatch_set_emacs_control_mode("2", manager, []() -> std::optional<std::string> {
            return "1";
        });

    bool ok = true;
    ok &= expect(!outcome.success, "dispatcher should fail for unmanaged workspace argument");
    ok &= expect(outcome.workspace_id.has_value() && *outcome.workspace_id == "2",
                 "dispatcher should preserve attempted workspace id");
    return ok;
}

bool test_dispatcher_falls_back_to_managed_workspace() {
    hyprmacs::WorkspaceManager manager;

    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-primary");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    manager.manage_workspace("1");

    const auto outcome = hyprmacs::dispatch_set_emacs_control_mode("", manager, []() -> std::optional<std::string> {
        return std::nullopt;
    });

    bool ok = true;
    ok &= expect(outcome.success, "dispatcher should fall back to managed workspace");
    ok &= expect(outcome.workspace_id.has_value() && *outcome.workspace_id == "1",
                 "fallback workspace should be managed workspace");
    ok &= expect(outcome.focus_client_id.has_value() && *outcome.focus_client_id == "0xeee",
                 "fallback should report managing emacs client focus target");
    return ok;
}

bool test_dispatcher_fails_without_target_workspace() {
    hyprmacs::WorkspaceManager manager;
    const auto outcome = hyprmacs::dispatch_set_emacs_control_mode("", manager, []() -> std::optional<std::string> {
        return std::nullopt;
    });

    bool ok = true;
    ok &= expect(!outcome.success, "dispatcher should fail without explicit, active, or managed workspace");
    ok &= expect(outcome.error == "no target workspace resolved",
                 "dispatcher should report missing workspace resolution");
    return ok;
}

bool test_manage_active_window_dispatcher_manages_active_client() {
    hyprmacs::WorkspaceManager manager;
    manager.manage_workspace("1");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("activewindowv2>>0xaaa");

    const auto outcome =
        hyprmacs::dispatch_manage_active_window("", manager, []() -> std::optional<std::string> {
            return "1";
        });

    bool ok = true;
    ok &= expect(outcome.success, "manage-active-window should succeed for eligible active client");
    ok &= expect(outcome.workspace_id.has_value() && *outcome.workspace_id == "1",
                 "manage-active-window should report workspace 1");
    ok &= expect(outcome.focus_client_id.has_value() && *outcome.focus_client_id == "0xaaa",
                 "manage-active-window should report managed client id");
    const auto state = manager.build_state_dump("1");
    ok &= expect(state.managed_clients == std::vector<std::string>({"0xaaa"}),
                 "manage-active-window should add active client to managed set");
    return ok;
}

bool test_manage_active_window_dispatcher_fails_without_eligible_active_client() {
    hyprmacs::WorkspaceManager manager;
    manager.manage_workspace("1");
    manager.process_event_for_tests("activewindowv2>>0xmissing");

    const auto outcome =
        hyprmacs::dispatch_manage_active_window("", manager, []() -> std::optional<std::string> {
            return "1";
        });

    bool ok = true;
    ok &= expect(!outcome.success, "manage-active-window should fail without eligible active client");
    ok &= expect(outcome.error == "no eligible active client in target workspace",
                 "manage-active-window should report missing active client");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_dispatcher_sets_emacs_control_and_reports_emacs_focus_target();
    ok &= test_dispatcher_fails_for_unmanaged_workspace_argument();
    ok &= test_dispatcher_falls_back_to_managed_workspace();
    ok &= test_dispatcher_fails_without_target_workspace();
    ok &= test_manage_active_window_dispatcher_manages_active_client();
    ok &= test_manage_active_window_dispatcher_fails_without_eligible_active_client();
    return ok ? 0 : 1;
}
