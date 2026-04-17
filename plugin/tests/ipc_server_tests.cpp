#include "hyprmacs/ipc_server.hpp"

#include <iostream>
#include <string>

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

bool test_route_set_layout_applies_visibility_and_geometry() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();
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

    ok &= expect(applier.is_hidden("0xaaa"), "hidden client should be hidden after set-layout");
    ok &= expect(!applier.is_hidden("0xbbb"), "visible client should not be hidden after set-layout");

    return ok;
}

bool test_route_set_layout_rejects_overlapping_rectangles() {
    hyprmacs::WorkspaceManager manager;
    auto applier = make_noop_applier();
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
    ok &= expect(!applier.is_hidden("0xaaa"), "overlap failure should not mutate visibility");
    ok &= expect(!applier.is_hidden("0xbbb"), "overlap failure should not mutate visibility");
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

}  // namespace

int main() {
    bool ok = true;
    ok &= test_route_manage_workspace();
    ok &= test_route_unmanage_workspace();
    ok &= test_route_request_state();
    ok &= test_route_debug_hide_show();
    ok &= test_route_set_selected_client_hides_non_selected();
    ok &= test_route_set_input_mode_updates_state_dump();
    ok &= test_route_seed_client_adopts_existing_window();
    ok &= test_route_set_layout_applies_visibility_and_geometry();
    ok &= test_route_set_layout_rejects_overlapping_rectangles();
    ok &= test_route_set_layout_ignores_non_managed_selected_client();
    ok &= test_route_set_layout_with_null_selected_client_does_not_pick_visible_client();
    return ok ? 0 : 1;
}
