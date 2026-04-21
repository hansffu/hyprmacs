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

bool test_apply_snapshot_hides_shows_and_moves() {
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
    ok &= expect(commands.size() == 5, "first snapshot should hide one and move/resize/move one");
    if (commands.size() >= 5) {
        ok &= expect(commands[0].find("dispatch movetoworkspacesilent special:hyprmacs-hidden,address:0xbbb")
                         != std::string::npos,
                     "first command should hide client");
        ok &= expect(commands[1].find("dispatch togglefloating address:0xaaa") != std::string::npos,
                     "second command should enable positioning mode");
        ok &= expect(commands[2].find("dispatch movewindowpixel exact 10 20,address:0xaaa") != std::string::npos,
                     "third command should move visible client");
        ok &= expect(commands[3].find("dispatch resizewindowpixel exact 300 400,address:0xaaa") != std::string::npos,
                     "fourth command should resize visible client");
        ok &= expect(commands[4].find("dispatch movewindowpixel exact 10 20,address:0xaaa") != std::string::npos,
                     "fifth command should re-anchor visible client after resize");
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
    ok &= expect(commands.size() == before_second + 6,
                 "second snapshot should hide old visible and show + move/resize/move new");
    if (commands.size() >= before_second + 6) {
        ok &= expect(commands[before_second].find("dispatch movetoworkspacesilent special:hyprmacs-hidden,address:0xaaa")
                         != std::string::npos,
                     "second snapshot should hide old visible client");
        ok &= expect(commands[before_second + 1].find("dispatch movetoworkspacesilent 1,address:0xbbb")
                         != std::string::npos,
                     "second snapshot should show previously hidden client");
        ok &= expect(commands[before_second + 2].find("dispatch togglefloating address:0xbbb") != std::string::npos,
                     "second snapshot should enable positioning mode for newly visible client");
        ok &= expect(commands[before_second + 5].find("dispatch movewindowpixel exact 1 2,address:0xbbb")
                         != std::string::npos,
                     "second snapshot should re-anchor newly visible client after resize");
    }
    return ok;
}

bool test_restore_workspace_to_native_shows_hidden_and_resets_positioning_mode() {
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
    ok &= expect(commands.size() == before_restore + 2,
                 "restore should show hidden client and disable positioning mode for visible managed client");
    if (commands.size() >= before_restore + 2) {
        ok &= expect(commands[before_restore].find("dispatch movetoworkspacesilent 1,address:0xbbb") != std::string::npos,
                     "restore should show hidden client in original workspace");
        ok &= expect(commands[before_restore + 1].find("dispatch togglefloating address:0xaaa") != std::string::npos,
                     "restore should disable temporary positioning mode");
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
    ok &= expect(commands.size() == before_reapply + 4,
                 "reapply should toggle positioning mode again after restore cleanup");
    if (commands.size() >= before_reapply + 1) {
        ok &= expect(commands[before_reapply].find("dispatch togglefloating address:0xaaa") != std::string::npos,
                     "first reapply command should re-enable positioning mode");
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
    ok &= test_apply_snapshot_hides_shows_and_moves();
    ok &= test_restore_workspace_to_native_shows_hidden_and_resets_positioning_mode();
    return ok ? 0 : 1;
}
