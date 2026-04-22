#include "hyprmacs/ipc_server.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

hyprmacs::LayoutApplier make_noop_applier() {
    return hyprmacs::LayoutApplier([](const std::string&) {
        return 0;
    });
}

struct RecordingApplier {
    std::vector<std::string> commands;

    hyprmacs::LayoutApplier make() {
        return hyprmacs::LayoutApplier([this](const std::string& command) {
            commands.push_back(command);
            return 0;
        });
    }
};

bool test_route_manage_workspace() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();

    const hyprmacs::ProtocolMessage incoming {
        .version = 1,
        .type = "manage-workspace",
        .workspace_id = "1",
        .timestamp = "2026-04-15T00:00:00Z",
        .payload_json = "{\"adopt_existing_clients\":true}",
    };

    const auto responses = hyprmacs::route_command_for_tests(incoming, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "manage-workspace should produce two responses");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "workspace-managed", "first response should be workspace-managed");
        ok &= expect(responses[1].type == "state-dump", "second response should be state-dump");
    }
    return ok;
}

bool test_route_manage_workspace_hides_existing_managed_clients() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");

    const hyprmacs::ProtocolMessage incoming {
        .version = 1,
        .type = "manage-workspace",
        .workspace_id = "1",
        .timestamp = "2026-04-18T00:00:00Z",
        .payload_json = "{\"adopt_existing_clients\":true}",
    };

    const auto responses = hyprmacs::route_command_for_tests(incoming, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "manage-workspace should produce two responses");
    ok &= expect(applier.is_hidden("0xaaa"), "manage-workspace should hide existing managed client 0xaaa");
    ok &= expect(applier.is_hidden("0xbbb"), "manage-workspace should hide existing managed client 0xbbb");
    return ok;
}

bool test_route_unmanage_workspace() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage incoming {
        .version = 1,
        .type = "unmanage-workspace",
        .workspace_id = "1",
        .timestamp = "2026-04-15T00:00:00Z",
        .payload_json = "{}",
    };

    const auto responses = hyprmacs::route_command_for_tests(incoming, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "unmanage-workspace should produce two responses");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "workspace-unmanaged", "first response should be workspace-unmanaged");
        ok &= expect(responses[1].type == "state-dump", "second response should be state-dump");
    }
    return ok;
}

bool test_route_request_state() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();

    const hyprmacs::ProtocolMessage incoming {
        .version = 1,
        .type = "request-state",
        .workspace_id = "1",
        .timestamp = "2026-04-15T00:00:00Z",
        .payload_json = "{}",
    };

    const auto responses = hyprmacs::route_command_for_tests(incoming, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 1, "request-state should produce one response");
    if (responses.size() == 1) {
        ok &= expect(responses[0].type == "state-dump", "response should be state-dump");
    }
    return ok;
}

bool test_route_debug_hide_show() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();

    const hyprmacs::ProtocolMessage hide {
        .version = 1,
        .type = "debug-hide-client",
        .workspace_id = "1",
        .timestamp = "2026-04-15T00:00:00Z",
        .payload_json = "{\"client_id\":\"0xabc\"}",
    };

    const auto hide_responses = hyprmacs::route_command_for_tests(hide, manager, applier);
    bool ok = true;
    ok &= expect(hide_responses.size() == 2, "debug-hide-client should produce two responses");
    if (hide_responses.size() == 2) {
        ok &= expect(hide_responses[0].type == "debug-hide-client-result", "first hide response should be debug result");
    }

    const hyprmacs::ProtocolMessage show {
        .version = 1,
        .type = "debug-show-client",
        .workspace_id = "1",
        .timestamp = "2026-04-15T00:00:00Z",
        .payload_json = "{\"client_id\":\"0xabc\"}",
    };

    const auto show_responses = hyprmacs::route_command_for_tests(show, manager, applier);
    ok &= expect(show_responses.size() == 2, "debug-show-client should produce two responses");
    if (show_responses.size() == 2) {
        ok &= expect(show_responses[0].type == "debug-show-client-result", "first show response should be debug result");
    }

    return ok;
}

bool test_route_set_selected_client_hides_non_selected() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();

    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage select {
        .version = 1,
        .type = "set-selected-client",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json = "{\"client_id\":\"0xaaa\"}",
    };

    const auto responses = hyprmacs::route_command_for_tests(select, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "set-selected-client should produce two responses");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[1].type == "state-dump", "second response should be state-dump");
    }

    ok &= expect(!applier.is_hidden("0xaaa"), "selected client should remain visible");
    ok &= expect(applier.is_hidden("0xbbb"), "non-selected managed client should be hidden");
    return ok;
}

bool test_route_set_input_mode_updates_state_dump() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage incoming {
        .version = 1,
        .type = "set-input-mode",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json = "{\"mode\":\"client-control\"}",
    };

    const auto responses = hyprmacs::route_command_for_tests(incoming, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "set-input-mode should produce two responses");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "mode-changed", "first response should be mode-changed");
        ok &= expect(responses[1].type == "state-dump", "second response should be state-dump");
        const auto payload = hyprmacs::parse_state_dump_payload(responses[1].payload_json);
        ok &= expect(payload.has_value(), "state-dump payload should parse");
        if (payload.has_value()) {
            ok &= expect(payload->input_mode.has_value(), "state-dump input_mode should be set");
            if (payload->input_mode.has_value()) {
                ok &= expect(*payload->input_mode == hyprmacs::InputMode::kClientControl,
                             "state-dump input_mode should be client-control");
            }
        }
    }
    return ok;
}

bool test_route_set_input_mode_emacs_control_focuses_managing_frame() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();
    std::string focused_command;
    hyprmacs::FocusController focus_controller([&focused_command](const std::string& command) {
        focused_command = command;
        return 0;
    });

    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-primary");
    manager.process_event_for_tests("openwindowv2>>0xfff,1,emacs,emacs-secondary");
    manager.process_event_for_tests("activewindowv2>>0xfff");
    manager.manage_workspace("1");
    manager.process_event_for_tests("activewindowv2>>0xeee");

    const hyprmacs::ProtocolMessage incoming {
        .version = 1,
        .type = "set-input-mode",
        .workspace_id = "1",
        .timestamp = "2026-04-18T00:00:00Z",
        .payload_json = "{\"mode\":\"emacs-control\"}",
    };

    const auto responses = hyprmacs::route_command_for_tests(incoming, manager, applier, &focus_controller);
    bool ok = true;
    ok &= expect(responses.size() == 2, "set-input-mode should produce two responses");
    ok &= expect(focused_command == "dispatch focuswindow address:0xfff",
                 "emacs-control should focus the managing emacs frame");
    return ok;
}

bool test_route_seed_client_adopts_existing_window() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage seed {
        .version = 1,
        .type = "seed-client",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json = "{\"client_id\":\"0xabc\",\"workspace_id\":\"1\",\"app_id\":\"foot\",\"title\":\"seeded\",\"floating\":false}",
    };
    const auto seed_responses = hyprmacs::route_command_for_tests(seed, manager, applier);
    bool ok = true;
    ok &= expect(seed_responses.size() == 1, "seed-client should produce one response");
    if (seed_responses.size() == 1) {
        ok &= expect(seed_responses[0].type == "state-dump", "seed-client response should be state-dump");
        const auto payload = hyprmacs::parse_state_dump_payload(seed_responses[0].payload_json);
        ok &= expect(payload.has_value(), "seed-client state-dump payload should parse");
        if (payload.has_value()) {
            ok &= expect(payload->managed_clients.size() == 1, "seeded eligible client should be adopted as managed");
            if (payload->managed_clients.size() == 1) {
                ok &= expect(payload->managed_clients[0] == "0xabc", "managed client should match seeded client");
            }
        }
    }
    return ok;
}

bool test_route_set_layout_commits_snapshot_without_geometry_application() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xbbb\",\"input_mode\":\"client-control\",\"visible_clients\":[\"0xbbb\"],"
            "\"hidden_clients\":[\"0xaaa\"],\"rectangles\":[{\"client_id\":\"0xbbb\",\"x\":10,\"y\":20,\"width\":300,\"height\":400}],"
            "\"stacking_order\":[\"0xbbb\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "set-layout should produce two responses");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[1].type == "state-dump", "second response should be state-dump");
        const auto payload = hyprmacs::parse_state_dump_payload(responses[1].payload_json);
        ok &= expect(payload.has_value(), "state-dump payload should parse");
        if (payload.has_value()) {
            ok &= expect(payload->input_mode.has_value(), "state-dump should include input mode");
            if (payload->input_mode.has_value()) {
                ok &= expect(*payload->input_mode == hyprmacs::InputMode::kClientControl,
                             "input mode should be client-control");
            }
            ok &= expect(payload->selected_client.has_value(), "state-dump should include selected client");
            if (payload->selected_client.has_value()) {
                ok &= expect(*payload->selected_client == "0xbbb", "selected client should be 0xbbb");
            }
        }
    }

    ok &= expect(recording.commands.empty(), "set-layout should not execute geometry commands");

    const auto snapshot = manager.managed_layout_snapshot("1");
    ok &= expect(snapshot.has_value(), "set-layout should commit a managed snapshot");
    if (snapshot.has_value()) {
        ok &= expect(snapshot->layout_version == 1, "first committed snapshot should have version 1");
        ok &= expect(snapshot->workspace_id == "1", "snapshot workspace should match route workspace");
        ok &= expect(snapshot->selected_client.has_value(), "snapshot should capture selected_client");
        if (snapshot->selected_client.has_value()) {
            ok &= expect(*snapshot->selected_client == "0xbbb", "snapshot selected client should be 0xbbb");
        }
        ok &= expect(snapshot->input_mode.has_value(), "snapshot should capture input_mode");
        if (snapshot->input_mode.has_value()) {
            ok &= expect(*snapshot->input_mode == hyprmacs::InputMode::kClientControl,
                         "snapshot input mode should be client-control");
        }
        ok &= expect(snapshot->visible_client_ids == std::vector<std::string>({"0xbbb"}),
                     "snapshot should preserve visible client list");
        ok &= expect(snapshot->hidden_client_ids == std::vector<std::string>({"0xaaa"}),
                     "snapshot should preserve hidden client list");
        ok &= expect(snapshot->stacking_order == std::vector<std::string>({"0xbbb"}),
                     "snapshot should preserve stacking order");
        ok &= expect(snapshot->rectangles_by_client_id.size() == 1, "snapshot should store one rectangle");
        if (snapshot->rectangles_by_client_id.size() == 1) {
            const auto it = snapshot->rectangles_by_client_id.find("0xbbb");
            ok &= expect(it != snapshot->rectangles_by_client_id.end(), "snapshot should store rectangle for 0xbbb");
            if (it != snapshot->rectangles_by_client_id.end()) {
                ok &= expect(it->second.x == 10 && it->second.y == 20 && it->second.width == 300 && it->second.height == 400,
                             "snapshot should preserve rectangle geometry");
            }
        }
    }

    return ok;
}

bool test_route_set_layout_rejects_missing_rectangle_for_visible_client_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\",\"0xbbb\"],"
            "\"hidden_clients\":[],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100}],"
            "\"stacking_order\":[\"0xaaa\",\"0xbbb\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "set-layout should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure when a visible client has no rectangle");
    }
    ok &= expect(recording.commands.empty(), "missing-rectangle validation should run before any geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "failed validation should not commit a snapshot");
    return ok;
}

bool test_route_set_layout_rejects_missing_required_arrays_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\"],"
            "\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100}],"
            "\"stacking_order\":[\"0xaaa\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "missing required arrays should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure when required arrays are missing");
    }
    ok &= expect(recording.commands.empty(), "missing-required-arrays validation should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "missing required arrays should not commit a snapshot");
    return ok;
}

bool test_route_set_layout_rejects_non_managed_or_non_eligible_members_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.manage_workspace("1");

    const auto before_state = manager.build_state_dump("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\",\"0xeee\"],"
            "\"hidden_clients\":[],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100},"
            "{\"client_id\":\"0xeee\",\"x\":110,\"y\":0,\"width\":100,\"height\":100}],\"stacking_order\":[\"0xaaa\",\"0xeee\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "invalid membership should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure for non-managed/non-eligible members");
    }
    ok &= expect(recording.commands.empty(), "membership validation should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "invalid membership should not commit a snapshot");
    const auto state = manager.build_state_dump("1");
    ok &= expect(state.selected_client == before_state.selected_client, "membership failure should preserve selected_client");
    ok &= expect(state.input_mode == before_state.input_mode, "membership failure should preserve input_mode");
    return ok;
}

bool test_route_set_layout_rejects_non_managed_hidden_member_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\"],"
            "\"hidden_clients\":[\"0xeee\"],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100}],"
            "\"stacking_order\":[\"0xaaa\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "invalid hidden member should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure for non-managed hidden members");
    }
    ok &= expect(recording.commands.empty(), "hidden membership validation should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "invalid hidden member should not commit a snapshot");
    return ok;
}

bool test_route_set_layout_rejects_duplicate_visible_clients_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\",\"0xaaa\"],"
            "\"hidden_clients\":[],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100}],"
            "\"stacking_order\":[\"0xaaa\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "duplicate visible IDs should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure for duplicate visible IDs");
    }
    ok &= expect(recording.commands.empty(), "duplicate visible IDs should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "duplicate visible IDs should not commit a snapshot");
    return ok;
}

bool test_route_set_layout_rejects_duplicate_hidden_clients_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\"],"
            "\"hidden_clients\":[\"0xaaa\",\"0xaaa\"],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100}],"
            "\"stacking_order\":[\"0xaaa\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "duplicate hidden IDs should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure for duplicate hidden IDs");
    }
    ok &= expect(recording.commands.empty(), "duplicate hidden IDs should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "duplicate hidden IDs should not commit a snapshot");
    return ok;
}

bool test_route_set_layout_rejects_visible_hidden_overlap_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\",\"0xbbb\"],"
            "\"hidden_clients\":[\"0xbbb\"],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100},"
            "{\"client_id\":\"0xbbb\",\"x\":110,\"y\":0,\"width\":100,\"height\":100}],\"stacking_order\":[\"0xaaa\",\"0xbbb\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "visible/hidden overlap should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure for visible/hidden overlap");
    }
    ok &= expect(recording.commands.empty(), "visible/hidden overlap should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "visible/hidden overlap should not commit a snapshot");
    return ok;
}

bool test_route_set_layout_rejects_duplicate_stacking_order_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\",\"0xbbb\"],"
            "\"hidden_clients\":[],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100},"
            "{\"client_id\":\"0xbbb\",\"x\":110,\"y\":0,\"width\":100,\"height\":100}],\"stacking_order\":[\"0xaaa\",\"0xaaa\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "duplicate stacking order should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure for duplicate stacking order");
    }
    ok &= expect(recording.commands.empty(), "duplicate stacking order should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "duplicate stacking order should not commit a snapshot");
    return ok;
}

bool test_route_set_layout_rejects_stacking_order_outside_visible_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\"],"
            "\"hidden_clients\":[\"0xbbb\"],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100}],"
            "\"stacking_order\":[\"0xaaa\",\"0xbbb\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "stacking outside visible should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure for stacking order outside visible");
    }
    ok &= expect(recording.commands.empty(), "stacking outside visible should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "stacking outside visible should not commit a snapshot");
    return ok;
}

bool test_route_set_layout_rejects_non_visible_rectangle_client_before_commit() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\"],"
            "\"hidden_clients\":[\"0xbbb\"],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100},"
            "{\"client_id\":\"0xbbb\",\"x\":110,\"y\":0,\"width\":100,\"height\":100}],\"stacking_order\":[\"0xaaa\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "non-visible rectangle client should still produce layout-applied and state-dump");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure for non-visible rectangle client");
    }
    ok &= expect(recording.commands.empty(), "non-visible rectangle client should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "non-visible rectangle client should not commit a snapshot");
    return ok;
}

bool test_route_set_layout_rejects_overlapping_rectangles() {
    hyprmacs::WorkspaceManager manager;
    RecordingApplier recording;
    auto applier = recording.make();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xbbb,1,foot,foot-b");
    manager.manage_workspace("1");

    const auto before_state = manager.build_state_dump("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xaaa\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\",\"0xbbb\"],"
            "\"hidden_clients\":[],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":0,\"y\":0,\"width\":100,\"height\":100},"
            "{\"client_id\":\"0xbbb\",\"x\":50,\"y\":50,\"width\":100,\"height\":100}],\"stacking_order\":[\"0xaaa\",\"0xbbb\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "set-layout overlap should still produce two responses");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":false") != std::string::npos,
                     "layout-applied should report failure for overlap");
    }
    ok &= expect(recording.commands.empty(), "overlap failure should not execute geometry commands");
    ok &= expect(!manager.managed_layout_snapshot("1").has_value(), "overlap failure should not commit a snapshot");
    const auto state = manager.build_state_dump("1");
    ok &= expect(state.selected_client == before_state.selected_client, "overlap failure should not mutate selected_client");
    ok &= expect(state.input_mode == before_state.input_mode, "overlap failure should not mutate input_mode");
    return ok;
}

bool test_route_set_layout_ignores_non_managed_selected_client() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.process_event_for_tests("openwindowv2>>0xeee,1,emacs,emacs-main");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-16T00:00:00Z",
        .payload_json =
            "{\"selected_client\":\"0xeee\",\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\"],"
            "\"hidden_clients\":[],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":10,\"y\":20,\"width\":300,\"height\":400}],"
            "\"stacking_order\":[\"0xaaa\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "set-layout should produce two responses");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":true") != std::string::npos,
                     "layout-applied should remain successful when selected client is non-managed");
        const auto payload = hyprmacs::parse_state_dump_payload(responses[1].payload_json);
        ok &= expect(payload.has_value(), "state-dump payload should parse");
        if (payload.has_value()) {
            ok &= expect(!payload->selected_client.has_value(),
                         "state-dump selected_client should remain empty for non-managed selection");
        }
    }
    return ok;
}

bool test_route_set_layout_with_null_selected_client_does_not_pick_visible_client() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();
    manager.process_event_for_tests("openwindowv2>>0xaaa,1,foot,foot-a");
    manager.manage_workspace("1");

    const hyprmacs::ProtocolMessage set_layout {
        .version = 1,
        .type = "set-layout",
        .workspace_id = "1",
        .timestamp = "2026-04-17T00:00:00Z",
        .payload_json =
            "{\"selected_client\":null,\"input_mode\":\"emacs-control\",\"visible_clients\":[\"0xaaa\"],"
            "\"hidden_clients\":[],\"rectangles\":[{\"client_id\":\"0xaaa\",\"x\":10,\"y\":20,\"width\":300,\"height\":400}],"
            "\"stacking_order\":[\"0xaaa\"]}",
    };

    const auto responses = hyprmacs::route_command_for_tests(set_layout, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 2, "set-layout should produce two responses");
    if (responses.size() == 2) {
        ok &= expect(responses[0].type == "layout-applied", "first response should be layout-applied");
        ok &= expect(responses[0].payload_json.find("\"ok\":true") != std::string::npos,
                     "layout-applied should succeed when selected_client is null");
        const auto payload = hyprmacs::parse_state_dump_payload(responses[1].payload_json);
        ok &= expect(payload.has_value(), "state-dump payload should parse");
        if (payload.has_value()) {
            ok &= expect(!payload->selected_client.has_value(),
                         "state-dump selected_client should stay empty when selected_client is null");
        }
    }
    return ok;
}

bool test_route_unknown_type_returns_protocol_error() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();

    const hyprmacs::ProtocolMessage unknown {
        .version = 1,
        .type = "totally-unknown-command",
        .workspace_id = "1",
        .timestamp = "2026-04-18T00:00:00Z",
        .payload_json = "{}",
    };

    const auto responses = hyprmacs::route_command_for_tests(unknown, manager, applier);
    bool ok = true;
    ok &= expect(responses.size() == 1, "unknown command should produce one protocol-error response");
    if (responses.size() == 1) {
        ok &= expect(responses[0].type == "protocol-error", "unknown command should respond with protocol-error");
        ok &= expect(responses[0].payload_json.find("\"code\":\"unknown-type\"") != std::string::npos,
                     "protocol-error should include unknown-type code");
        ok &= expect(responses[0].payload_json.find("totally-unknown-command") != std::string::npos,
                     "protocol-error should include unknown command detail");
    }
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_route_manage_workspace();
    ok &= test_route_manage_workspace_hides_existing_managed_clients();
    ok &= test_route_unmanage_workspace();
    ok &= test_route_request_state();
    ok &= test_route_debug_hide_show();
    ok &= test_route_set_selected_client_hides_non_selected();
    ok &= test_route_set_input_mode_updates_state_dump();
    ok &= test_route_set_input_mode_emacs_control_focuses_managing_frame();
    ok &= test_route_seed_client_adopts_existing_window();
    ok &= test_route_set_layout_rejects_non_managed_or_non_eligible_members_before_commit();
    ok &= test_route_set_layout_rejects_non_managed_hidden_member_before_commit();
    ok &= test_route_set_layout_rejects_duplicate_visible_clients_before_commit();
    ok &= test_route_set_layout_rejects_duplicate_hidden_clients_before_commit();
    ok &= test_route_set_layout_rejects_visible_hidden_overlap_before_commit();
    ok &= test_route_set_layout_rejects_duplicate_stacking_order_before_commit();
    ok &= test_route_set_layout_rejects_stacking_order_outside_visible_before_commit();
    ok &= test_route_set_layout_rejects_non_visible_rectangle_client_before_commit();
    ok &= test_route_set_layout_commits_snapshot_without_geometry_application();
    ok &= test_route_set_layout_rejects_missing_required_arrays_before_commit();
    ok &= test_route_set_layout_rejects_missing_rectangle_for_visible_client_before_commit();
    ok &= test_route_set_layout_rejects_overlapping_rectangles();
    ok &= test_route_set_layout_ignores_non_managed_selected_client();
    ok &= test_route_set_layout_with_null_selected_client_does_not_pick_visible_client();
    ok &= test_route_unknown_type_returns_protocol_error();
    return ok ? 0 : 1;
}
