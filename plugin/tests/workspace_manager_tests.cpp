#include "hyprmacs/workspace_manager.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool test_parse_event_frame() {
    bool ok = true;

    const auto parsed = hyprmacs::parse_event_frame("openwindow>>0x123,workspace-1,foot");
    ok &= expect(parsed.has_value(), "expected valid frame to parse");
    if (parsed.has_value()) {
        ok &= expect(parsed->name == "openwindow", "expected openwindow name");
        ok &= expect(parsed->payload == "0x123,workspace-1,foot", "expected payload parse");
    }

    ok &= expect(!hyprmacs::parse_event_frame("").has_value(), "empty line should fail parse");
    ok &= expect(!hyprmacs::parse_event_frame("no-delimiter").has_value(), "line without delimiter should fail");
    ok &= expect(!hyprmacs::parse_event_frame(">>missing-name").has_value(), "line without event name should fail");

    return ok;
}

bool test_tracked_event_names() {
    bool ok = true;

    ok &= expect(hyprmacs::is_tracked_event_name("openwindow"), "openwindow should be tracked");
    ok &= expect(hyprmacs::is_tracked_event_name("openwindowv2"), "openwindowv2 should be tracked");
    ok &= expect(hyprmacs::is_tracked_event_name("activewindowv2"), "activewindowv2 should be tracked");
    ok &= expect(hyprmacs::is_tracked_event_name("windowtitlev2"), "windowtitlev2 should be tracked");
    ok &= expect(hyprmacs::is_tracked_event_name("changefloatingmode"), "changefloatingmode should be tracked");
    ok &= expect(hyprmacs::is_tracked_event_name("changefloatingmodev2"), "changefloatingmodev2 should be tracked");
    ok &= expect(hyprmacs::is_tracked_event_name("somethingfloating"), "floating suffix should be tracked");
    ok &= expect(!hyprmacs::is_tracked_event_name("workspace"), "workspace should not be tracked");

    return ok;
}

bool test_internal_hidden_workspace_move_keeps_managed_membership() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    auto before_hide_move = manager.build_state_dump("1");
    ok &= expect(before_hide_move.managed_clients.size() == 2, "both workspace clients should be managed before hide move");

    manager.process_event_for_tests("movewindowv2>>0xaaa,-98");

    const auto after_hide_move = manager.build_state_dump("1");
    ok &= expect(after_hide_move.managed_clients.size() == 2,
                 "internal hide move should not remove client from managed set");

    return ok;
}

bool test_state_dump_excludes_non_managed_selected_client() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.manage_workspace("1");
    manager.process_event_for_tests("activewindowv2>>0xeee");

    const auto state = manager.build_state_dump("1");
    ok &= expect(!state.selected_client.has_value(),
                 "state-dump should not expose selected_client when focused client is not managed");

    return ok;
}

bool test_workspace_policy_applied_and_restored_on_manage_unmanage() {
    bool ok = true;

    std::vector<std::string> dispatched_commands;
    std::unordered_map<std::string, std::string> query_replies {
        {"j/getoption input:follow_mouse", "{\"int\":1}"},
        {"j/getoption animations:enabled", "{\"int\":1}"},
        {"j/getoption misc:focus_on_activate", "{\"int\":1}"},
    };

    hyprmacs::WorkspaceManager manager(
        [&dispatched_commands](const std::string& command) {
            dispatched_commands.push_back(command);
            return 0;
        },
        [&query_replies](const std::string& command) -> std::optional<std::string> {
            const auto it = query_replies.find(command);
            if (it == query_replies.end()) {
                return std::nullopt;
            }
            return it->second;
        }
    );

    manager.manage_workspace("1");
    ok &= expect(dispatched_commands.size() == 3, "manage should apply three policy keyword commands");
    if (dispatched_commands.size() >= 3) {
        ok &= expect(dispatched_commands[0] == "keyword input:follow_mouse 0", "manage should disable follow_mouse");
        ok &= expect(dispatched_commands[1] == "keyword animations:enabled 0", "manage should disable animations");
        ok &= expect(dispatched_commands[2] == "keyword misc:focus_on_activate 0", "manage should disable focus_on_activate");
    }

    manager.unmanage_workspace("1");
    ok &= expect(dispatched_commands.size() == 6, "unmanage should restore three policy keyword commands");
    if (dispatched_commands.size() >= 6) {
        ok &= expect(dispatched_commands[3] == "keyword input:follow_mouse 1", "unmanage should restore follow_mouse");
        ok &= expect(dispatched_commands[4] == "keyword animations:enabled 1", "unmanage should restore animations");
        ok &= expect(dispatched_commands[5] == "keyword misc:focus_on_activate 1", "unmanage should restore focus_on_activate");
    }

    return ok;
}

bool test_workspace_policy_restored_on_controller_disconnect() {
    bool ok = true;

    std::vector<std::string> dispatched_commands;
    std::unordered_map<std::string, std::string> query_replies {
        {"j/getoption input:follow_mouse", "{\"int\":0}"},
        {"j/getoption animations:enabled", "{\"int\":0}"},
        {"j/getoption misc:focus_on_activate", "{\"int\":1}"},
    };

    hyprmacs::WorkspaceManager manager(
        [&dispatched_commands](const std::string& command) {
            dispatched_commands.push_back(command);
            return 0;
        },
        [&query_replies](const std::string& command) -> std::optional<std::string> {
            const auto it = query_replies.find(command);
            if (it == query_replies.end()) {
                return std::nullopt;
            }
            return it->second;
        }
    );

    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.manage_workspace("1");
    manager.set_controller_connected(true);
    manager.set_controller_connected(false);

    const auto state = manager.build_state_dump("1");
    ok &= expect(!state.managed, "disconnect should unmanage workspace");
    ok &= expect(state.input_mode == std::nullopt, "disconnect should clear input mode");
    ok &= expect(dispatched_commands.size() == 6, "disconnect path should apply and restore policy keywords");
    if (dispatched_commands.size() >= 6) {
        ok &= expect(dispatched_commands[3] == "keyword input:follow_mouse 0", "disconnect should restore snapshot follow_mouse");
        ok &= expect(dispatched_commands[4] == "keyword animations:enabled 0", "disconnect should restore snapshot animations");
        ok &= expect(dispatched_commands[5] == "keyword misc:focus_on_activate 1", "disconnect should restore snapshot focus_on_activate");
    }

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_parse_event_frame();
    ok &= test_tracked_event_names();
    ok &= test_internal_hidden_workspace_move_keeps_managed_membership();
    ok &= test_state_dump_excludes_non_managed_selected_client();
    ok &= test_workspace_policy_applied_and_restored_on_manage_unmanage();
    ok &= test_workspace_policy_restored_on_controller_disconnect();
    return ok ? 0 : 1;
}
