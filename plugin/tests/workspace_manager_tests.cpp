#include "hyprmacs/workspace_manager.hpp"

#include <algorithm>
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

bool test_float_state_transitions_update_managed_set_and_emit_notifications() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    std::vector<std::string> notifications;

    manager.set_client_transition_notifier(
        [&notifications](const hyprmacs::WorkspaceId& workspace_id, const hyprmacs::ClientId& client_id, bool floating) {
            notifications.push_back(
                (floating ? "client-became-floating:" : "client-became-tiled:") + workspace_id + ":" + client_id
            );
        }
    );

    manager.set_controller_connected(true);
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    auto before = manager.build_state_dump("1");
    ok &= expect(before.managed_clients.size() == 2, "both clients should be managed before float transition");

    manager.process_event_for_tests("changefloatingmodev2>>0xaaa,true");
    auto after_floating = manager.build_state_dump("1");
    ok &= expect(after_floating.managed_clients.size() == 1, "floating client should be removed from managed set");
    ok &= expect(std::find(after_floating.managed_clients.begin(), after_floating.managed_clients.end(), "0xaaa")
                     == after_floating.managed_clients.end(),
                 "floating client should not stay in managed list");
    ok &= expect(notifications.size() == 1, "floating transition should emit one notification");
    if (notifications.size() >= 1) {
        ok &= expect(notifications[0] == "client-became-floating:1:0xaaa",
                     "floating transition should emit client-became-floating");
    }

    manager.process_event_for_tests("changefloatingmodev2>>0xaaa,false");
    auto after_tiled = manager.build_state_dump("1");
    ok &= expect(after_tiled.managed_clients.size() == 2, "client should re-enter managed set after tiling");
    ok &= expect(std::find(after_tiled.managed_clients.begin(), after_tiled.managed_clients.end(), "0xaaa")
                     != after_tiled.managed_clients.end(),
                 "retiled client should re-enter managed list");
    ok &= expect(notifications.size() == 2, "tiling transition should emit a second notification");
    if (notifications.size() >= 2) {
        ok &= expect(notifications[1] == "client-became-tiled:1:0xaaa",
                     "tiling transition should emit client-became-tiled");
    }

    return ok;
}

bool test_openwindow_refreshes_floating_state_from_clients_query() {
    bool ok = true;

    std::string clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"pcmanfm","title":"About PCManFM","floating":true}
    ])";

    hyprmacs::WorkspaceManager manager(
        [](const std::string&) { return 0; },
        [&clients_json](const std::string& command) -> std::optional<std::string> {
            if (command == "j/clients") {
                return clients_json;
            }
            return std::nullopt;
        }
    );

    manager.manage_workspace("1");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,pcmanfm,About PCManFM");
    const auto state = manager.build_state_dump("1");

    ok &= expect(state.managed_clients.empty(), "floating client discovered on open should not be managed");
    ok &= expect(state.eligible_clients.empty(), "floating client discovered on open should not be eligible");
    return ok;
}

bool test_refresh_based_float_transition_notifier_without_floating_event() {
    bool ok = true;

    std::vector<std::string> notifications;
    std::string clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"foot-a","floating":false}
    ])";

    hyprmacs::WorkspaceManager manager(
        [](const std::string&) { return 0; },
        [&clients_json](const std::string& command) -> std::optional<std::string> {
            if (command == "j/clients") {
                return clients_json;
            }
            return std::nullopt;
        }
    );

    manager.set_client_transition_notifier(
        [&notifications](const hyprmacs::WorkspaceId& workspace_id, const hyprmacs::ClientId& client_id, bool floating) {
            notifications.push_back(
                (floating ? "client-became-floating:" : "client-became-tiled:") + workspace_id + ":" + client_id
            );
        }
    );
    manager.set_controller_connected(true);
    manager.manage_workspace("1");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    auto state = manager.build_state_dump("1");
    ok &= expect(state.managed_clients.size() == 1, "client should start as managed when query reports tiled");

    clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"foot-a","floating":true}
    ])";
    manager.process_event_for_tests("activewindowv2>>0xaaa");
    state = manager.build_state_dump("1");
    ok &= expect(state.managed_clients.empty(), "client should leave managed set when query flips to floating");
    ok &= expect(notifications.size() == 1, "floating transition from query refresh should notify once");
    if (notifications.size() >= 1) {
        ok &= expect(notifications[0] == "client-became-floating:1:0xaaa", "floating notification payload mismatch");
    }

    clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"foot-a","floating":false}
    ])";
    manager.process_event_for_tests("activewindowv2>>0xaaa");
    state = manager.build_state_dump("1");
    ok &= expect(state.managed_clients.size() == 1, "client should re-enter managed set when query flips to tiled");
    ok &= expect(notifications.size() == 2, "tiled transition from query refresh should notify again");
    if (notifications.size() >= 2) {
        ok &= expect(notifications[1] == "client-became-tiled:1:0xaaa", "tiled notification payload mismatch");
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
    ok &= test_float_state_transitions_update_managed_set_and_emit_notifications();
    ok &= test_openwindow_refreshes_floating_state_from_clients_query();
    ok &= test_refresh_based_float_transition_notifier_without_floating_event();
    return ok ? 0 : 1;
}
