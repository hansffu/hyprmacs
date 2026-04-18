#include "hyprmacs/dispatchers.hpp"

#include <iostream>
#include <optional>
#include <string>

#include "hyprmacs/focus_controller.hpp"
#include "hyprmacs/workspace_manager.hpp"

namespace {

bool expect(bool cond, const char* message) {
    if (!cond) {
        std::cerr << "[FAIL] " << message << '\n';
    }
    return cond;
}

bool test_dispatcher_sets_emacs_control_and_focuses_emacs() {
    hyprmacs::WorkspaceManager manager;
    std::string focused_command;
    hyprmacs::FocusController focus_controller([&focused_command](const std::string& command) {
        focused_command = command;
        return 0;
    });

    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-primary");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    manager.manage_workspace("1");
    manager.set_input_mode("1", hyprmacs::InputMode::kClientControl);

    const auto outcome =
        hyprmacs::dispatch_set_emacs_control_mode("", manager, focus_controller, []() -> std::optional<std::string> {
            return "1";
        });

    bool ok = true;
    ok &= expect(outcome.success, "dispatcher should succeed for managed workspace");
    ok &= expect(outcome.workspace_id.has_value() && *outcome.workspace_id == "1",
                 "dispatcher should report workspace 1");
    ok &= expect(focused_command == "dispatch focuswindow address:0xeee",
                 "dispatcher should focus managing emacs client");

    const auto state = manager.build_state_dump("1");
    ok &= expect(state.input_mode.has_value() && *state.input_mode == hyprmacs::InputMode::kEmacsControl,
                 "dispatcher should set input mode to emacs-control");
    return ok;
}

bool test_dispatcher_fails_for_unmanaged_workspace_argument() {
    hyprmacs::WorkspaceManager manager;
    hyprmacs::FocusController focus_controller([](const std::string&) {
        return 0;
    });
    manager.manage_workspace("1");

    const auto outcome =
        hyprmacs::dispatch_set_emacs_control_mode("2", manager, focus_controller, []() -> std::optional<std::string> {
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
    std::string focused_command;
    hyprmacs::FocusController focus_controller([&focused_command](const std::string& command) {
        focused_command = command;
        return 0;
    });

    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-primary");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    manager.manage_workspace("1");

    const auto outcome = hyprmacs::dispatch_set_emacs_control_mode("", manager, focus_controller, []() -> std::optional<std::string> {
        return std::nullopt;
    });

    bool ok = true;
    ok &= expect(outcome.success, "dispatcher should fall back to managed workspace");
    ok &= expect(outcome.workspace_id.has_value() && *outcome.workspace_id == "1",
                 "fallback workspace should be managed workspace");
    ok &= expect(!focused_command.empty(), "dispatcher should attempt focus for managing emacs client");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_dispatcher_sets_emacs_control_and_focuses_emacs();
    ok &= test_dispatcher_fails_for_unmanaged_workspace_argument();
    ok &= test_dispatcher_falls_back_to_managed_workspace();
    return ok ? 0 : 1;
}
