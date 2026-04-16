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

}  // namespace

int main() {
    bool ok = true;
    ok &= test_route_manage_workspace();
    ok &= test_route_unmanage_workspace();
    ok &= test_route_request_state();
    ok &= test_route_debug_hide_show();
    return ok ? 0 : 1;
}
