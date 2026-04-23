#include "hyprmacs/layout_applier.hpp"

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

bool test_hide_then_restore_roundtrip() {
    std::vector<std::string> commands;

    hyprmacs::LayoutApplier applier([&commands](const std::string& command) {
        commands.push_back(command);
        return 0;
    });

    bool ok = true;

    ok &= expect(applier.hide_client("0xabc", "1"), "hide should succeed");
    ok &= expect(commands.size() == 1, "hide should issue one command");
    if (commands.size() == 1) {
        ok &= expect(commands[0].find("dispatch movetoworkspacesilent special:hyprmacs-hidden,address:0xabc")
                         != std::string::npos,
                     "hide command should target hidden workspace");
    }

    ok &= expect(applier.show_client("0xabc"), "show should succeed");
    ok &= expect(commands.size() == 2, "show should issue second command");
    if (commands.size() == 2) {
        ok &= expect(commands[1].find("dispatch movetoworkspacesilent 1,address:0xabc") != std::string::npos,
                     "show command should restore original workspace");
    }

    return ok;
}

bool test_show_without_hidden_state() {
    hyprmacs::LayoutApplier applier([](const std::string&) {
        return 0;
    });
    return expect(applier.show_client("0xmissing"), "show should be idempotent when client is not hidden");
}

bool test_hide_does_not_duplicate_commands() {
    int call_count = 0;
    hyprmacs::LayoutApplier applier([&call_count](const std::string&) {
        call_count += 1;
        return 0;
    });

    bool ok = true;
    ok &= expect(applier.hide_client("0xabc", "1"), "first hide should succeed");
    ok &= expect(applier.hide_client("0xabc", "1"), "second hide should be idempotent success");
    ok &= expect(call_count == 1, "only one command should be issued");
    return ok;
}

bool test_id_normalization_across_hide_show() {
    std::vector<std::string> commands;
    hyprmacs::LayoutApplier applier([&commands](const std::string& command) {
        commands.push_back(command);
        return 0;
    });

    bool ok = true;
    ok &= expect(applier.hide_client("55c31e4088a0", "1"), "hide without 0x should succeed");
    ok &= expect(applier.is_hidden("0x55c31e4088a0"), "is_hidden should accept normalized form");
    ok &= expect(applier.show_client("0x55c31e4088a0"), "show with 0x should succeed");
    ok &= expect(commands.size() == 2, "normalized hide/show should issue two commands");
    return ok;
}

bool test_apply_snapshot_rejects_overlapping_rectangles() {
    hyprmacs::LayoutApplier applier([](const std::string&) {
        return 0;
    });

    std::string error;
    const bool ok = applier.apply_snapshot(
        "1",
        {
            {.client_id = "0xaaa", .x = 0, .y = 0, .width = 100, .height = 100},
            {.client_id = "0xbbb", .x = 50, .y = 50, .width = 100, .height = 100},
        },
        {},
        {},
        &error
    );
    return expect(!ok && error.find("overlapping rectangles") != std::string::npos,
                  "overlapping rectangles should be rejected");
}

bool test_apply_snapshot_hides_and_shows_without_geometry_commands() {
    std::vector<std::string> commands;
    hyprmacs::LayoutApplier applier([&commands](const std::string& command) {
        commands.push_back(command);
        return 0;
    });

    const bool first_ok = applier.apply_snapshot(
        "1",
        {{.client_id = "0xaaa", .x = 10, .y = 20, .width = 300, .height = 400}},
        {"0xbbb"},
        {"0xaaa"},
        nullptr
    );

    bool ok = true;
    ok &= expect(first_ok, "first snapshot should apply");
    ok &= expect(commands.size() == 1, "first snapshot should only hide clients");
    if (commands.size() >= 1) {
        ok &= expect(commands[0].find("dispatch movetoworkspacesilent special:hyprmacs-hidden,address:0xbbb")
                         != std::string::npos,
                     "first command should hide client");
    }

    const size_t before_second = commands.size();
    const bool second_ok = applier.apply_snapshot(
        "1",
        {{.client_id = "0xbbb", .x = 1, .y = 2, .width = 200, .height = 210}},
        {"0xaaa"},
        {"0xbbb"},
        nullptr
    );
    ok &= expect(second_ok, "second snapshot should apply");
    ok &= expect(commands.size() == before_second + 2,
                 "second snapshot should hide old visible and show newly visible client");
    if (commands.size() >= before_second + 2) {
        ok &= expect(commands[before_second].find("dispatch movetoworkspacesilent special:hyprmacs-hidden,address:0xaaa")
                         != std::string::npos,
                     "second snapshot should hide old visible client");
        ok &= expect(commands[before_second + 1].find("dispatch movetoworkspacesilent 1,address:0xbbb")
                         != std::string::npos,
                     "second snapshot should show previously hidden client");
    }
    return ok;
}

bool test_restore_workspace_to_native_shows_hidden_clients_only() {
    std::vector<std::string> commands;
    hyprmacs::LayoutApplier applier([&commands](const std::string& command) {
        commands.push_back(command);
        return 0;
    });

    bool ok = true;
    ok &= expect(applier.apply_snapshot(
                     "1",
                     {{.client_id = "0xaaa", .x = 10, .y = 20, .width = 300, .height = 400}},
                     {"0xbbb"},
                     {"0xaaa"},
                     nullptr
                 ),
                 "snapshot should apply before restore");

    const size_t before_restore = commands.size();
    ok &= expect(applier.restore_workspace_to_native("1", {"0xaaa", "0xbbb"}),
                 "restore_workspace_to_native should succeed");
    ok &= expect(commands.size() == before_restore + 1,
                 "restore should only show hidden clients");
    if (commands.size() >= before_restore + 1) {
        ok &= expect(commands[before_restore].find("dispatch movetoworkspacesilent 1,address:0xbbb") != std::string::npos,
                     "restore should show hidden client in original workspace");
    }

    const size_t before_reapply = commands.size();
    ok &= expect(applier.apply_snapshot(
                     "1",
                     {{.client_id = "0xaaa", .x = 1, .y = 2, .width = 200, .height = 210}},
                     {},
                     {"0xaaa"},
                     nullptr
                 ),
                 "snapshot should apply after restore");
    ok &= expect(commands.size() == before_reapply, "reapply should not emit geometry commands");

    return ok;
}

bool test_overlay_floating_commands_are_emitted_with_normalized_ids() {
    std::vector<std::string> commands;
    hyprmacs::LayoutApplier applier([&commands](const std::string& command) {
        commands.push_back(command);
        return 0;
    });

    bool ok = true;
    ok &= expect(applier.ensure_client_floating("aaa"), "ensure_client_floating should succeed");
    ok &= expect(applier.apply_floating_geometry({.client_id = "aaa", .x = 10, .y = 20, .width = 300, .height = 400}),
                 "apply_floating_geometry should succeed");
    ok &= expect(applier.lower_client_zorder("aaa"), "lower_client_zorder should succeed");

    ok &= expect(commands.size() == 5, "overlay helpers should emit five commands");
    if (commands.size() == 5) {
        ok &= expect(commands[0] == "dispatch setfloating address:0xaaa",
                     "ensure_client_floating should normalize client id");
        ok &= expect(commands[1] == "dispatch movewindowpixel exact 10 20,address:0xaaa",
                     "apply_floating_geometry should emit movewindowpixel exact command");
        ok &= expect(commands[2] == "dispatch resizewindowpixel exact 300 400,address:0xaaa",
                     "apply_floating_geometry should emit resizewindowpixel exact command");
        ok &= expect(commands[3] == "dispatch movewindowpixel exact 10 20,address:0xaaa",
                     "apply_floating_geometry should re-anchor after resize");
        ok &= expect(commands[4] == "dispatch alterzorder bottom,address:0xaaa",
                     "lower_client_zorder should emit alterzorder bottom command");
    }

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_hide_then_restore_roundtrip();
    ok &= test_show_without_hidden_state();
    ok &= test_hide_does_not_duplicate_commands();
    ok &= test_id_normalization_across_hide_show();
    ok &= test_apply_snapshot_rejects_overlapping_rectangles();
    ok &= test_apply_snapshot_hides_and_shows_without_geometry_commands();
    ok &= test_restore_workspace_to_native_shows_hidden_clients_only();
    ok &= test_overlay_floating_commands_are_emitted_with_normalized_ids();
    return ok ? 0 : 1;
}
