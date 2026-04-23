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
        {"j/workspaces", R"([{"id":1,"tiledLayout":"master"}])"},
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
    ok &= expect(dispatched_commands.size() == 3, "manage should set layout and apply two policy keyword commands");
    if (dispatched_commands.size() >= 3) {
        ok &= expect(dispatched_commands[0] == "keyword workspace 1,layout:hyprmacs",
                     "manage should switch workspace tiled layout to hyprmacs");
        ok &= expect(dispatched_commands[1] == "keyword animations:enabled 0", "manage should disable animations");
        ok &= expect(dispatched_commands[2] == "keyword misc:focus_on_activate 0", "manage should disable focus_on_activate");
    }

    manager.unmanage_workspace("1");
    ok &= expect(dispatched_commands.size() == 6,
                 "unmanage should restore workspace layout and two policy keyword commands");
    if (dispatched_commands.size() >= 6) {
        ok &= expect(dispatched_commands[3] == "keyword workspace 1,layout:master",
                     "unmanage should restore previous workspace tiled layout");
        ok &= expect(dispatched_commands[4] == "keyword animations:enabled 1", "unmanage should restore animations");
        ok &= expect(dispatched_commands[5] == "keyword misc:focus_on_activate 1", "unmanage should restore focus_on_activate");
    }

    return ok;
}

bool test_workspace_policy_restored_on_controller_disconnect() {
    bool ok = true;

    std::vector<std::string> dispatched_commands;
    std::unordered_map<std::string, std::string> query_replies {
        {"j/workspaces", R"([{"id":1,"tiledLayout":"master"}])"},
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
    ok &= expect(dispatched_commands.size() == 6, "disconnect path should restore workspace layout and policy keywords");
    if (dispatched_commands.size() >= 6) {
        ok &= expect(dispatched_commands[3] == "keyword workspace 1,layout:master",
                     "disconnect should restore previous workspace tiled layout");
        ok &= expect(dispatched_commands[4] == "keyword animations:enabled 0", "disconnect should restore snapshot animations");
        ok &= expect(dispatched_commands[5] == "keyword misc:focus_on_activate 1", "disconnect should restore snapshot focus_on_activate");
    }

    return ok;
}

bool test_manage_workspace_idempotent_does_not_leak_policy_lease() {
    bool ok = true;

    std::vector<std::string> dispatched_commands;
    std::unordered_map<std::string, std::string> query_replies {
        {"j/workspaces", R"([{"id":1,"tiledLayout":"master"}])"},
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
    manager.manage_workspace("1");
    manager.unmanage_workspace("1");

    ok &= expect(dispatched_commands.size() == 6,
                 "repeated manage on same workspace should not duplicate policy/layout dispatches");
    if (dispatched_commands.size() >= 6) {
        ok &= expect(dispatched_commands[3] == "keyword workspace 1,layout:master",
                     "unmanage should restore original layout after idempotent manage");
    }

    return ok;
}

bool test_manage_workspace_tracks_managing_emacs_frame() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-primary");
    manager.process_event_for_tests("openwindowv2>>0xfff,1,emacs,emacs-secondary");
    manager.process_event_for_tests("activewindowv2>>0xfff");

    manager.manage_workspace("1");
    auto emacs = manager.emacs_client("1");
    ok &= expect(emacs.has_value(), "manage should discover a managing emacs frame");
    if (emacs.has_value()) {
        ok &= expect(*emacs == "0xfff", "manage should bind to the focused emacs frame");
    }

    manager.process_event_for_tests("activewindowv2>>0xeee");
    emacs = manager.emacs_client("1");
    ok &= expect(emacs.has_value(), "managing emacs frame should remain available");
    if (emacs.has_value()) {
        ok &= expect(*emacs == "0xfff", "focusing another emacs frame should not replace managing frame");
    }

    manager.process_event_for_tests("closewindow>>0xfff");
    emacs = manager.emacs_client("1");
    ok &= expect(emacs.has_value(), "manager close should fall back to remaining emacs frame");
    if (emacs.has_value()) {
        ok &= expect(*emacs == "0xeee", "fallback managing emacs frame should be the remaining emacs client");
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

bool test_manage_workspace_refreshes_floating_state_from_clients_query() {
    bool ok = true;

    std::string clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"floating-before-manage","floating":false}
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

    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,floating-before-manage");
    clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"floating-before-manage","floating":true}
    ])";
    manager.manage_workspace("1");
    const auto state = manager.build_state_dump("1");

    ok &= expect(state.managed_clients.empty(),
                 "manage should refresh floating state and exclude pre-existing floating client from managed set");
    ok &= expect(state.eligible_clients.empty(),
                 "manage should refresh floating state and exclude pre-existing floating client from eligible set");
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
    manager.manage_workspace("1");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    auto state = manager.build_state_dump("1");
    ok &= expect(state.managed_clients.size() == 1, "client should start as managed when query reports tiled");
    ok &= expect(notifications.size() == 1, "managed client open should emit tiled transition for immediate hide path");
    if (notifications.size() >= 1) {
        ok &= expect(notifications[0] == "client-became-tiled:1:0xaaa",
                     "open-managed notification payload mismatch");
    }

    clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"foot-a","floating":true}
    ])";
    manager.process_event_for_tests("activewindowv2>>0xaaa");
    state = manager.build_state_dump("1");
    ok &= expect(state.managed_clients.empty(), "client should leave managed set when query flips to floating");
    ok &= expect(notifications.size() == 2, "floating transition from query refresh should notify");
    if (notifications.size() >= 2) {
        ok &= expect(notifications[1] == "client-became-floating:1:0xaaa", "floating notification payload mismatch");
    }

    clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"foot-a","floating":false}
    ])";
    manager.process_event_for_tests("activewindowv2>>0xaaa");
    state = manager.build_state_dump("1");
    ok &= expect(state.managed_clients.size() == 1, "client should re-enter managed set when query flips to tiled");
    ok &= expect(notifications.size() == 3, "tiled transition from query refresh should notify again");
    if (notifications.size() >= 3) {
        ok &= expect(notifications[2] == "client-became-tiled:1:0xaaa", "tiled notification payload mismatch");
    }

    return ok;
}

bool test_open_managed_client_emits_transition_without_controller_connection() {
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

    manager.manage_workspace("1");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,pcmanfm,hansffu");

    const auto state = manager.build_state_dump("1");
    ok &= expect(state.managed_clients.size() == 1, "opened tiled client should enter managed set");
    ok &= expect(notifications.size() == 1,
                 "managed client open should still emit tiled transition without controller connection");
    if (notifications.size() >= 1) {
        ok &= expect(notifications[0] == "client-became-tiled:1:0xaaa",
                     "open-managed transition payload should match client");
    }

    return ok;
}

bool test_state_change_notifier_on_focus_and_close_events() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    std::vector<std::string> state_updates;
    manager.set_state_change_notifier([&state_updates](const hyprmacs::WorkspaceId& workspace_id) {
        state_updates.push_back(workspace_id);
    });

    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");
    manager.set_controller_connected(true);
    state_updates.clear();

    manager.process_event_for_tests("activewindowv2>>0xbbb");
    ok &= expect(state_updates.size() == 1, "focus change should emit one workspace state update");
    if (!state_updates.empty()) {
        ok &= expect(state_updates[0] == "1", "focus state update should target managed workspace");
    }

    manager.process_event_for_tests("closewindow>>0xbbb");
    ok &= expect(state_updates.size() == 2, "closewindow should emit another workspace state update");
    if (state_updates.size() >= 2) {
        ok &= expect(state_updates[1] == "1", "close state update should target managed workspace");
    }

    return ok;
}

bool test_managed_layout_snapshot_apply_get_and_versioning() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    manager.manage_workspace("1");
    ok &= expect(manager.set_selected_client("1", "0xaaa"), "should set managed selected client before apply");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "snapshot should start empty before first apply");
    ok &= expect(!manager.apply_managed_layout_snapshot({
                    .workspace_id = "2",
                    .layout_version = 0,
                    .rectangles_by_client_id = {},
                    .visible_client_ids = {},
                    .hidden_client_ids = {},
                    .stacking_order = {},
                    .selected_client = std::nullopt,
                    .input_mode = std::nullopt,
                    .managing_emacs_client_id = std::nullopt,
                }),
                "apply should reject snapshots for non-managed workspaces");

    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 99,
        .rectangles_by_client_id = {
            {"0xaaa", hyprmacs::ClientRect {.x = 0, .y = 0, .width = 100, .height = 100}},
            {"0xbbb", hyprmacs::ClientRect {.x = 100, .y = 0, .width = 100, .height = 100}},
        },
        .visible_client_ids = {"0xaaa"},
        .hidden_client_ids = {"0xbbb"},
        .stacking_order = {"0xaaa", "0xbbb"},
        .selected_client = "0xnope",
        .input_mode = hyprmacs::InputMode::kUnknown,
        .managing_emacs_client_id = "0xnope",
    };

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "first snapshot apply should succeed");

    auto stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "stored snapshot should be retrievable");
    if (stored.has_value()) {
        ok &= expect(stored->workspace_id == "1", "stored snapshot should retain workspace id");
        ok &= expect(stored->layout_version == 1, "first stored snapshot should get version 1");
        ok &= expect(stored->rectangles_by_client_id.size() == 2, "stored snapshot should keep rectangles");
        ok &= expect(stored->visible_client_ids == std::vector<std::string>({"0xaaa"}),
                     "stored snapshot should keep visible clients");
        ok &= expect(stored->hidden_client_ids == std::vector<std::string>({"0xbbb"}),
                     "stored snapshot should keep hidden clients");
        ok &= expect(stored->stacking_order == std::vector<std::string>({"0xaaa", "0xbbb"}),
                     "stored snapshot should keep stacking order");
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xaaa",
                     "stored snapshot should use manager-selected client");
        ok &= expect(stored->input_mode == hyprmacs::InputMode::kEmacsControl,
                     "stored snapshot should use manager input mode");
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xeee",
                     "stored snapshot should resolve managing emacs client id");
    }

    manager.set_selected_client("1", "0xaaa");
    manager.set_input_mode("1", hyprmacs::InputMode::kClientControl);

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "second snapshot apply should succeed");

    stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "updated snapshot should still be retrievable");
    if (stored.has_value()) {
        ok &= expect(stored->layout_version == 2, "second stored snapshot should increment version");
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xaaa",
                     "second stored snapshot should keep manager-selected client");
        ok &= expect(stored->input_mode == hyprmacs::InputMode::kClientControl,
                     "second stored snapshot should keep manager input mode");
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xeee",
                     "second stored snapshot should keep manager-resolved emacs client id");
    }

    manager.set_selected_client("1", "0xaaa");
    manager.set_input_mode("1", hyprmacs::InputMode::kEmacsControl);
    manager.process_event_for_tests("activewindowv2>>0xaaa");
    stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should remain available after manager-state mutations");
    if (stored.has_value()) {
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xaaa",
                     "snapshot should track later selected_client changes");
        ok &= expect(stored->input_mode == hyprmacs::InputMode::kEmacsControl,
                     "snapshot should track later input mode changes");
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xeee",
                     "snapshot should track later managing emacs refreshes");
    }

    manager.clear_managed_layout_snapshot("1");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "clear should remove stored snapshot");

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "snapshot should apply after explicit clear");
    stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value() && stored->layout_version == 3, "layout version should remain monotonic after clear");

    return ok;
}

bool test_managed_layout_snapshot_rejects_non_managed_workspace_and_clears_on_switch() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    manager.manage_workspace("1");
    hyprmacs::ManagedWorkspaceLayoutSnapshot first {
        .workspace_id = "1",
        .layout_version = 0,
        .rectangles_by_client_id = {},
        .visible_client_ids = {"0xaaa"},
        .hidden_client_ids = {},
        .stacking_order = {"0xaaa"},
        .selected_client = std::nullopt,
        .input_mode = hyprmacs::InputMode::kEmacsControl,
        .managing_emacs_client_id = std::nullopt,
    };
    hyprmacs::ManagedWorkspaceLayoutSnapshot rejected {
        .workspace_id = "2",
        .layout_version = 0,
        .rectangles_by_client_id = {},
        .visible_client_ids = {"0xbbb"},
        .hidden_client_ids = {},
        .stacking_order = {"0xbbb"},
        .selected_client = std::nullopt,
        .input_mode = hyprmacs::InputMode::kClientControl,
        .managing_emacs_client_id = std::nullopt,
    };

    ok &= expect(manager.apply_managed_layout_snapshot(first), "first workspace snapshot should apply");
    ok &= expect(!manager.apply_managed_layout_snapshot(rejected), "apply should reject non-managed workspace");
    ok &= expect(!manager.managed_layout_snapshot("2").has_value(), "rejected snapshot should not be stored");

    manager.manage_workspace("2");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "switching managed workspace should clear previous snapshot");
    ok &= expect(!manager.apply_managed_layout_snapshot(first), "stale workspace snapshot should remain rejected after switch");

    return ok;
}

bool test_controller_disconnect_clears_active_managed_layout_snapshot() {
    bool ok = true;

    std::vector<std::string> dispatched_commands;
    std::unordered_map<std::string, std::string> query_replies {
        {"j/workspaces", R"([{"id":1,"tiledLayout":"master"},{"id":2,"tiledLayout":"dwindle"}])"},
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

    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    ok &= expect(manager.manage_workspace("1"), "workspace 1 should become managed before disconnect");
    ok &= expect(manager.set_selected_client("1", "0xaaa"), "managed selected client should be set before disconnect");

    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 44,
        .rectangles_by_client_id = {},
        .visible_client_ids = {},
        .hidden_client_ids = {},
        .stacking_order = {},
        .selected_client = std::nullopt,
        .input_mode = std::nullopt,
        .managing_emacs_client_id = std::nullopt,
    };

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "snapshot should apply before disconnect");
    ok &= expect(manager.managed_layout_snapshot("1").has_value(), "workspace 1 snapshot should exist before disconnect");

    manager.set_controller_connected(true);
    manager.set_controller_connected(false);

    ok &= expect(!manager.managed_layout_snapshot("1").has_value(),
                 "disconnect should clear active workspace snapshot");

    return ok;
}

bool test_managing_emacs_refresh_keeps_committed_snapshot_in_sync() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xfff,1,emacs,emacs-alt");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    ok &= expect(manager.manage_workspace("1"), "workspace 1 should become managed");

    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 0,
        .rectangles_by_client_id = {},
        .visible_client_ids = {"0xaaa"},
        .hidden_client_ids = {},
        .stacking_order = {"0xaaa"},
        .selected_client = std::nullopt,
        .input_mode = std::nullopt,
        .managing_emacs_client_id = std::nullopt,
    };

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "snapshot should apply");
    auto stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should be present after apply");
    if (stored.has_value()) {
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xeee",
                     "snapshot should capture initial managing emacs");
    }

    manager.process_event_for_tests("activewindowv2>>0xfff");
    stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should stay present after fallback-prepping focus change");
    if (stored.has_value()) {
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xeee",
                     "snapshot should stay coherent before fallback assignment");
    }

    manager.process_event_for_tests("closewindow>>0xeee");
    stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should stay present after managing emacs closes");
    if (stored.has_value()) {
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xfff",
                     "snapshot should update to last_active managing emacs on close fallback");
    }

    manager.process_event_for_tests("activewindowv2>>0xaaa");
    stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should stay present after focus change");
    if (stored.has_value()) {
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xfff",
                     "snapshot should remain coherent after non-emacs focus change");
    }

    manager.process_event_for_tests("activewindowv2>>0xfff");
    stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should stay present after emacs refocus");
    if (stored.has_value()) {
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xfff",
                     "snapshot should stay synchronized with managing emacs refresh");
    }

    return ok;
}

bool test_activewindow_updates_committed_snapshot_selected_client() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    ok &= expect(manager.manage_workspace("1"), "workspace 1 should become managed");
    ok &= expect(manager.set_selected_client("1", "0xaaa"), "workspace 1 should start with a managed selected client");

    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 0,
        .rectangles_by_client_id = {},
        .visible_client_ids = {"0xaaa", "0xbbb"},
        .hidden_client_ids = {},
        .stacking_order = {"0xaaa", "0xbbb"},
        .selected_client = std::nullopt,
        .input_mode = std::nullopt,
        .managing_emacs_client_id = std::nullopt,
    };

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "snapshot should apply");
    auto stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should exist after apply");
    if (stored.has_value()) {
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xaaa",
                     "initial committed selected client should reflect the managed selection");
    }

    manager.process_event_for_tests("activewindowv2>>0xbbb");
    stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should remain available after activewindowv2");
    if (stored.has_value()) {
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xbbb",
                     "activewindowv2 should update committed selected client");
    }

    return ok;
}

bool test_committed_snapshot_prunes_closed_managed_clients() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    ok &= expect(manager.manage_workspace("1"), "workspace 1 should become managed");
    ok &= expect(manager.set_selected_client("1", "0xaaa"), "workspace 1 should start with a managed selected client");

    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 0,
        .rectangles_by_client_id = {
            {"0xaaa", hyprmacs::ClientRect {.x = 0, .y = 0, .width = 100, .height = 100}},
            {"0xbbb", hyprmacs::ClientRect {.x = 100, .y = 0, .width = 100, .height = 100}},
        },
        .visible_client_ids = {"0xaaa", "0xbbb"},
        .hidden_client_ids = {},
        .stacking_order = {"0xbbb", "0xaaa"},
        .selected_client = std::nullopt,
        .input_mode = std::nullopt,
        .managing_emacs_client_id = std::nullopt,
    };

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "snapshot should apply");
    manager.process_event_for_tests("closewindow>>0xbbb");

    auto stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should remain after client close");
    if (stored.has_value()) {
        ok &= expect(stored->rectangles_by_client_id.find("0xbbb") == stored->rectangles_by_client_id.end(),
                     "closed client should be pruned from rectangles");
        ok &= expect(std::find(stored->visible_client_ids.begin(), stored->visible_client_ids.end(), "0xbbb") ==
                         stored->visible_client_ids.end(),
                     "closed client should be pruned from visible_clients");
        ok &= expect(std::find(stored->stacking_order.begin(), stored->stacking_order.end(), "0xbbb") ==
                         stored->stacking_order.end(),
                     "closed client should be pruned from stacking_order");
        ok &= expect(std::find(stored->hidden_client_ids.begin(), stored->hidden_client_ids.end(), "0xbbb") ==
                         stored->hidden_client_ids.end(),
                     "closed client should be pruned from hidden_clients");
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xaaa",
                     "selection should remain coherent after close pruning");
    }

    return ok;
}

bool test_committed_snapshot_prunes_moved_managed_clients() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    ok &= expect(manager.manage_workspace("1"), "workspace 1 should become managed");
    ok &= expect(manager.set_selected_client("1", "0xaaa"), "workspace 1 should start with a managed selected client");

    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 0,
        .rectangles_by_client_id = {
            {"0xaaa", hyprmacs::ClientRect {.x = 0, .y = 0, .width = 100, .height = 100}},
            {"0xbbb", hyprmacs::ClientRect {.x = 100, .y = 0, .width = 100, .height = 100}},
        },
        .visible_client_ids = {"0xaaa", "0xbbb"},
        .hidden_client_ids = {},
        .stacking_order = {"0xbbb", "0xaaa"},
        .selected_client = std::nullopt,
        .input_mode = std::nullopt,
        .managing_emacs_client_id = std::nullopt,
    };

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "snapshot should apply");
    manager.process_event_for_tests("movewindowv2>>0xbbb,2");

    auto stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should remain after move transition");
    if (stored.has_value()) {
        ok &= expect(stored->rectangles_by_client_id.find("0xbbb") == stored->rectangles_by_client_id.end(),
                     "moved client should be pruned from rectangles");
        ok &= expect(std::find(stored->visible_client_ids.begin(), stored->visible_client_ids.end(), "0xbbb") ==
                         stored->visible_client_ids.end(),
                     "moved client should be pruned from visible_clients");
        ok &= expect(std::find(stored->stacking_order.begin(), stored->stacking_order.end(), "0xbbb") ==
                         stored->stacking_order.end(),
                     "moved client should be pruned from stacking_order");
        ok &= expect(std::find(stored->hidden_client_ids.begin(), stored->hidden_client_ids.end(), "0xbbb") ==
                         stored->hidden_client_ids.end(),
                     "moved client should be pruned from hidden_clients");
        ok &= expect(stored->rectangles_by_client_id.find("0xaaa") != stored->rectangles_by_client_id.end(),
                     "still-managed client should remain in rectangles");
        ok &= expect(std::find(stored->visible_client_ids.begin(), stored->visible_client_ids.end(), "0xaaa") !=
                         stored->visible_client_ids.end(),
                     "still-managed client should remain visible");
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xaaa",
                     "selection should remain coherent after pruning a different client");
    }

    return ok;
}

bool test_seed_client_refreshes_committed_snapshot_coherence() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xfff,1,emacs,emacs-alt");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    ok &= expect(manager.manage_workspace("1"), "workspace 1 should become managed");
    ok &= expect(manager.set_selected_client("1", "0xaaa"), "workspace 1 should start with a managed selected client");

    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 0,
        .rectangles_by_client_id = {
            {"0xaaa", hyprmacs::ClientRect {.x = 0, .y = 0, .width = 100, .height = 100}},
            {"0xbbb", hyprmacs::ClientRect {.x = 100, .y = 0, .width = 100, .height = 100}},
        },
        .visible_client_ids = {"0xaaa", "0xbbb"},
        .hidden_client_ids = {},
        .stacking_order = {"0xbbb", "0xaaa"},
        .selected_client = std::nullopt,
        .input_mode = std::nullopt,
        .managing_emacs_client_id = std::nullopt,
    };

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "snapshot should apply");

    manager.seed_client("0xbbb", "1", "foot", "foot-b", true);

    auto stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should remain after seed client prune");
    if (stored.has_value()) {
        ok &= expect(stored->rectangles_by_client_id.find("0xbbb") == stored->rectangles_by_client_id.end(),
                     "seeded floating client should be pruned from rectangles");
        ok &= expect(std::find(stored->visible_client_ids.begin(), stored->visible_client_ids.end(), "0xbbb") ==
                         stored->visible_client_ids.end(),
                     "seeded floating client should be pruned from visible clients");
        ok &= expect(std::find(stored->stacking_order.begin(), stored->stacking_order.end(), "0xbbb") ==
                         stored->stacking_order.end(),
                     "seeded floating client should be pruned from stacking order");
        ok &= expect(std::find(stored->hidden_client_ids.begin(), stored->hidden_client_ids.end(), "0xbbb") ==
                         stored->hidden_client_ids.end(),
                     "seeded floating client should be pruned from hidden clients");
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xaaa",
                     "selected client should stay coherent after seed client prune");
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xeee",
                     "managing emacs should stay coherent after seed client prune");
    }

    manager.seed_client("0xeee", "1", "foot", "foot-main", false);

    stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should remain after managing emacs reseed");
    if (stored.has_value()) {
        ok &= expect(stored->managing_emacs_client_id.has_value() && *stored->managing_emacs_client_id == "0xfff",
                     "seed client should refresh managing emacs resolution");
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xaaa",
                     "selected client should remain coherent after managing emacs reseed");
        ok &= expect(stored->rectangles_by_client_id.find("0xaaa") != stored->rectangles_by_client_id.end(),
                     "still-managed client should remain in rectangles");
        ok &= expect(stored->rectangles_by_client_id.find("0xbbb") == stored->rectangles_by_client_id.end(),
                     "pruned client should stay removed from rectangles");
    }

    return ok;
}

bool test_seed_client_inserts_new_managed_client_hidden_by_default() {
    bool ok = true;

    hyprmacs::WorkspaceManager manager;
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    ok &= expect(manager.manage_workspace("1"), "workspace 1 should become managed");
    ok &= expect(manager.set_selected_client("1", "0xaaa"), "workspace 1 should start with a managed selected client");

    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 0,
        .rectangles_by_client_id = {
            {"0xaaa", hyprmacs::ClientRect {.x = 0, .y = 0, .width = 100, .height = 100}},
        },
        .visible_client_ids = {"0xaaa"},
        .hidden_client_ids = {},
        .stacking_order = {"0xaaa"},
        .selected_client = std::nullopt,
        .input_mode = std::nullopt,
        .managing_emacs_client_id = std::nullopt,
    };

    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "snapshot should apply");

    manager.seed_client("0xbbb", "1", "foot", "foot-b", false);

    auto stored = manager.managed_layout_snapshot("1");
    ok &= expect(stored.has_value(), "snapshot should remain after new managed client seed");
    if (stored.has_value()) {
        ok &= expect(stored->rectangles_by_client_id.find("0xbbb") == stored->rectangles_by_client_id.end(),
                     "newly managed client should not have a rectangle until replanned");
        ok &= expect(std::find(stored->hidden_client_ids.begin(), stored->hidden_client_ids.end(), "0xbbb") !=
                         stored->hidden_client_ids.end(),
                     "newly managed client should be inserted hidden by default");
        ok &= expect(std::find(stored->visible_client_ids.begin(), stored->visible_client_ids.end(), "0xbbb") ==
                         stored->visible_client_ids.end(),
                     "newly managed client should not be visible before replanning");
        ok &= expect(std::find(stored->stacking_order.begin(), stored->stacking_order.end(), "0xbbb") ==
                         stored->stacking_order.end(),
                     "newly managed client should not be stacked before replanning");
        ok &= expect(stored->selected_client.has_value() && *stored->selected_client == "0xaaa",
                     "selection should stay coherent after inserting a new managed client");
    }

    return ok;
}

bool test_visible_snapshot_client_stays_managed_when_query_reports_floating_overlay() {
    bool ok = true;

    std::string clients_json = R"([
      {"address":"0xeee","workspace":{"id":1},"class":"emacs","title":"emacs-main","floating":false},
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

    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("activewindowv2>>0xeee");
    ok &= expect(manager.manage_workspace("1"), "workspace 1 should become managed");
    ok &= expect(manager.set_selected_client("1", "0xaaa"), "workspace 1 should set selected managed client");

    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 0,
        .rectangles_by_client_id = {
            {"0xaaa", hyprmacs::ClientRect {.x = 10, .y = 20, .width = 300, .height = 400}},
        },
        .visible_client_ids = {"0xaaa"},
        .hidden_client_ids = {},
        .stacking_order = {"0xaaa"},
        .selected_client = std::nullopt,
        .input_mode = std::nullopt,
        .managing_emacs_client_id = std::nullopt,
    };
    ok &= expect(manager.apply_managed_layout_snapshot(snapshot), "snapshot should apply");
    manager.note_overlay_float_request("1", "0xaaa");

    clients_json = R"([
      {"address":"0xeee","workspace":{"id":1},"class":"emacs","title":"emacs-main","floating":false},
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"foot-a","floating":true}
    ])";
    const bool mutated = manager.refresh_workspace_floating_state_from_query("1");
    const auto state = manager.build_state_dump("1");

    ok &= expect(!mutated, "visible snapshot overlay floating refresh should not mutate managed registry state");
    ok &= expect(state.managed_clients == std::vector<std::string>({"0xaaa"}),
                 "visible snapshot client should stay managed when query reports floating");

    return ok;
}

bool test_unmanaged_only_floating_refresh_adopts_tiled_client_without_reclassifying_managed() {
    bool ok = true;

    std::string clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"foot-a","floating":false},
      {"address":"0xbbb","workspace":{"id":1},"class":"foot","title":"foot-b","floating":false}
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

    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    ok &= expect(manager.manage_workspace("1"), "workspace 1 should become managed");
    manager.process_event_for_tests("changefloatingmodev2>>0xbbb,true");

    auto state = manager.build_state_dump("1");
    ok &= expect(state.managed_clients == std::vector<std::string>({"0xaaa"}),
                 "setup should leave 0xaaa managed and 0xbbb unmanaged");

    clients_json = R"([
      {"address":"0xaaa","workspace":{"id":1},"class":"foot","title":"foot-a","floating":true},
      {"address":"0xbbb","workspace":{"id":1},"class":"foot","title":"foot-b","floating":false}
    ])";

    const bool mutated = manager.refresh_workspace_floating_state_from_query("1", false);
    state = manager.build_state_dump("1");
    ok &= expect(mutated, "unmanaged-only refresh should mutate when an unmanaged client becomes tiled");
    ok &= expect(state.managed_clients.size() == 2, "unmanaged-only refresh should adopt retiled unmanaged client");
    ok &= expect(std::find(state.managed_clients.begin(), state.managed_clients.end(), "0xaaa") != state.managed_clients.end(),
                 "unmanaged-only refresh should not drop existing managed client");
    ok &= expect(std::find(state.managed_clients.begin(), state.managed_clients.end(), "0xbbb") != state.managed_clients.end(),
                 "unmanaged-only refresh should adopt unmanaged client that is tiled in query");

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
    ok &= test_manage_workspace_idempotent_does_not_leak_policy_lease();
    ok &= test_manage_workspace_tracks_managing_emacs_frame();
    ok &= test_float_state_transitions_update_managed_set_and_emit_notifications();
    ok &= test_openwindow_refreshes_floating_state_from_clients_query();
    ok &= test_manage_workspace_refreshes_floating_state_from_clients_query();
    ok &= test_refresh_based_float_transition_notifier_without_floating_event();
    ok &= test_open_managed_client_emits_transition_without_controller_connection();
    ok &= test_state_change_notifier_on_focus_and_close_events();
    ok &= test_managed_layout_snapshot_apply_get_and_versioning();
    ok &= test_managed_layout_snapshot_rejects_non_managed_workspace_and_clears_on_switch();
    ok &= test_controller_disconnect_clears_active_managed_layout_snapshot();
    ok &= test_managing_emacs_refresh_keeps_committed_snapshot_in_sync();
    ok &= test_activewindow_updates_committed_snapshot_selected_client();
    ok &= test_committed_snapshot_prunes_closed_managed_clients();
    ok &= test_committed_snapshot_prunes_moved_managed_clients();
    ok &= test_seed_client_refreshes_committed_snapshot_coherence();
    ok &= test_seed_client_inserts_new_managed_client_hidden_by_default();
    ok &= test_visible_snapshot_client_stays_managed_when_query_reports_floating_overlay();
    ok &= test_unmanaged_only_floating_refresh_adopts_tiled_client_without_reclassifying_managed();
    return ok ? 0 : 1;
}
