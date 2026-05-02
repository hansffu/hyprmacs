#include "hyprmacs/layout_applier.hpp"
#include "hyprmacs/workspace_manager.hpp"

#include <hyprland/src/helpers/math/Math.hpp>

#include <cstdint>
#include <iostream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hyprmacs {

enum class ManagedTargetVisibilityAction {
    kIgnore,
    kShow,
    kHide,
};

std::optional<CBox> compute_managed_target_box_for_recalc(const ManagedWorkspaceLayoutSnapshot& snapshot,
                                                          std::string_view target_workspace_id,
                                                          std::string_view target_client_id,
                                                          bool target_floating,
                                                          const CBox& work_area);
std::optional<std::string> build_workspace_recalc_dispatch_command(std::string_view workspace_id);
bool request_workspace_recalc_marshaled(const WorkspaceId& workspace_id, const std::function<int(const std::string&)>& dispatcher);
std::optional<std::string> build_client_zorder_dispatch_command(std::string_view client_id, bool top);
bool request_client_zorder_marshaled(std::string_view client_id, bool top, const std::function<int(const std::string&)>& dispatcher);
std::vector<LayoutRectangle> visible_rectangles_from_snapshot(const ManagedWorkspaceLayoutSnapshot& snapshot);
bool emacs_control_repair_generation_is_current(uint64_t current_generation, uint64_t repair_generation);
ManagedTargetVisibilityAction compute_managed_target_visibility_action_for_recalc(
    const ManagedWorkspaceLayoutSnapshot& snapshot,
    std::string_view target_workspace_id,
    std::string_view target_client_id,
    bool target_floating,
    bool target_is_hidden
);

}

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool expect_box(const std::optional<CBox>& actual, const CBox& expected, const std::string& message) {
    if (!actual.has_value()) {
        std::cerr << "FAIL: " << message << " (missing box)\n";
        return false;
    }

    if (!(actual.value() == expected)) {
        std::cerr << "FAIL: " << message << " expected=(" << expected.x << "," << expected.y << "," << expected.w << ","
                  << expected.h << ") actual=(" << actual->x << "," << actual->y << "," << actual->w << "," << actual->h
                  << ")\n";
        return false;
    }

    return true;
}

hyprmacs::ManagedWorkspaceLayoutSnapshot sample_snapshot() {
    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot;
    snapshot.workspace_id = "1";
    snapshot.visible_client_ids = {"0xaaa"};
    snapshot.hidden_client_ids = {"0xbbb"};
    snapshot.stacking_order = {"0xaaa"};
    snapshot.rectangles_by_client_id.emplace("0xaaa", hyprmacs::ClientRect {
        .x = 10,
        .y = 20,
        .width = 300,
        .height = 400,
    });
    snapshot.managing_emacs_client_id = "0xeee";
    return snapshot;
}

bool test_recalc_box_pins_managing_emacs_to_work_area() {
    const auto snapshot = sample_snapshot();
    const auto actual = hyprmacs::compute_managed_target_box_for_recalc(
        snapshot,
        "1",
        "address:eee",
        false,
        CBox {1, 2, 111, 222}
    );

    return expect_box(actual, CBox {1, 2, 111, 222}, "managing emacs target should pin to workspace work area");
}

bool test_recalc_box_places_visible_managed_client_from_snapshot_rectangle() {
    const auto snapshot = sample_snapshot();
    const auto actual = hyprmacs::compute_managed_target_box_for_recalc(
        snapshot,
        "1",
        "aaa",
        false,
        CBox {0, 0, 999, 999}
    );

    return expect_box(actual, CBox {10, 20, 300, 400}, "visible managed client should use snapshot rectangle");
}

bool test_recalc_box_ignores_hidden_unmanaged_floating_and_wrong_workspace_targets() {
    bool ok = true;
    const auto snapshot = sample_snapshot();

    ok &= expect(!hyprmacs::compute_managed_target_box_for_recalc(snapshot, "1", "0xbbb", false, CBox {0, 0, 1, 1}).has_value(),
                 "hidden managed client should not receive a geometry box");
    ok &= expect(!hyprmacs::compute_managed_target_box_for_recalc(snapshot, "1", "0xccc", false, CBox {0, 0, 1, 1}).has_value(),
                 "unmanaged client should not receive a geometry box");
    ok &= expect(!hyprmacs::compute_managed_target_box_for_recalc(snapshot, "1", "0xaaa", true, CBox {0, 0, 1, 1}).has_value(),
                 "native floating target should not receive managed geometry");
    ok &= expect(!hyprmacs::compute_managed_target_box_for_recalc(snapshot, "2", "0xaaa", false, CBox {0, 0, 1, 1}).has_value(),
                 "target on another workspace should not receive managed geometry");

    return ok;
}

bool test_recalc_visibility_shows_visible_targets_and_hides_hidden_targets() {
    bool ok = true;
    const auto snapshot = sample_snapshot();

    ok &= expect(
        hyprmacs::compute_managed_target_visibility_action_for_recalc(snapshot, "1", "0xaaa", false, false) ==
            hyprmacs::ManagedTargetVisibilityAction::kShow,
        "visible managed client should request show action"
    );
    ok &= expect(
        hyprmacs::compute_managed_target_visibility_action_for_recalc(snapshot, "1", "bbb", false, false) ==
            hyprmacs::ManagedTargetVisibilityAction::kHide,
        "hidden managed client should request hide action"
    );

    return ok;
}

bool test_recalc_visibility_ignores_emacs_unmanaged_floating_and_wrong_workspace_targets() {
    bool ok = true;
    const auto snapshot = sample_snapshot();

    ok &= expect(
        hyprmacs::compute_managed_target_visibility_action_for_recalc(snapshot, "1", "0xeee", false, false) ==
            hyprmacs::ManagedTargetVisibilityAction::kIgnore,
        "managing emacs target should not be hide/show managed"
    );
    ok &= expect(
        hyprmacs::compute_managed_target_visibility_action_for_recalc(snapshot, "1", "0xccc", false, false) ==
            hyprmacs::ManagedTargetVisibilityAction::kIgnore,
        "unmanaged target should not be hide/show managed"
    );
    ok &= expect(
        hyprmacs::compute_managed_target_visibility_action_for_recalc(snapshot, "1", "0xaaa", true, false) ==
            hyprmacs::ManagedTargetVisibilityAction::kIgnore,
        "floating target should not be hide/show managed"
    );
    ok &= expect(
        hyprmacs::compute_managed_target_visibility_action_for_recalc(snapshot, "2", "0xaaa", false, false) ==
            hyprmacs::ManagedTargetVisibilityAction::kIgnore,
        "wrong-workspace target should not be hide/show managed"
    );

    return ok;
}

bool test_recalc_visibility_restores_hidden_clients_marked_visible_by_snapshot() {
    bool ok = true;
    const auto snapshot = sample_snapshot();

    ok &= expect(
        hyprmacs::compute_managed_target_visibility_action_for_recalc(snapshot, "-99", "0xaaa", false, true) ==
            hyprmacs::ManagedTargetVisibilityAction::kShow,
        "hidden client should be shown when snapshot marks it visible"
    );
    ok &= expect(
        hyprmacs::compute_managed_target_visibility_action_for_recalc(snapshot, "-99", "0xbbb", false, true) ==
            hyprmacs::ManagedTargetVisibilityAction::kHide,
        "hidden client should stay hidden when snapshot marks it hidden"
    );
    ok &= expect(
        hyprmacs::compute_managed_target_visibility_action_for_recalc(snapshot, "-99", "0xaaa", true, true) ==
            hyprmacs::ManagedTargetVisibilityAction::kIgnore,
        "floating hidden target should remain unmanaged by visibility action"
    );

    return ok;
}

bool test_build_workspace_recalc_dispatch_command_normalizes_input() {
    bool ok = true;

    const auto command = hyprmacs::build_workspace_recalc_dispatch_command(" 1 ");
    ok &= expect(command.has_value(), "non-empty workspace id should build recalc dispatch command");
    if (command.has_value()) {
        ok &= expect(*command == "dispatch hyprmacs:request-recalc 1",
                     "recalc dispatch command should target hyprmacs:request-recalc");
    }

    ok &= expect(!hyprmacs::build_workspace_recalc_dispatch_command("   ").has_value(),
                 "blank workspace id should not build recalc dispatch command");

    return ok;
}

bool test_request_workspace_recalc_marshaled_dispatches_via_injected_executor() {
    bool ok = true;
    std::vector<std::string> commands;

    const bool dispatched = hyprmacs::request_workspace_recalc_marshaled(
        "1",
        [&commands](const std::string& command) {
            commands.push_back(command);
            return 0;
        }
    );
    ok &= expect(dispatched, "valid workspace id should dispatch recalc marshal command");
    ok &= expect(commands == std::vector<std::string>({"dispatch hyprmacs:request-recalc 1"}),
                 "marshalled recalc should use dispatcher command socket path");

    commands.clear();
    ok &= expect(
        !hyprmacs::request_workspace_recalc_marshaled(
            "   ",
            [&commands](const std::string& command) {
                commands.push_back(command);
                return 0;
            }
        ),
        "blank workspace id should not dispatch recalc marshal command"
    );
    ok &= expect(commands.empty(), "blank workspace id should not invoke dispatcher");

    return ok;
}

bool test_build_client_zorder_dispatch_command_normalizes_input() {
    bool ok = true;

    const auto top_command = hyprmacs::build_client_zorder_dispatch_command(" address:abc ", true);
    ok &= expect(top_command.has_value(), "non-empty client id should build top z-order dispatch command");
    if (top_command.has_value()) {
        ok &= expect(*top_command == "dispatch alterzorder top,address:0xabc",
                     "z-order top command should normalize address and prefix");
    }

    const auto bottom_command = hyprmacs::build_client_zorder_dispatch_command("0xdef", false);
    ok &= expect(bottom_command.has_value(), "non-empty client id should build bottom z-order dispatch command");
    if (bottom_command.has_value()) {
        ok &= expect(*bottom_command == "dispatch alterzorder bottom,address:0xdef",
                     "z-order bottom command should preserve normalized id");
    }

    ok &= expect(!hyprmacs::build_client_zorder_dispatch_command("   ", true).has_value(),
                 "blank client id should not build z-order dispatch command");

    return ok;
}

bool test_request_client_zorder_marshaled_dispatches_via_injected_executor() {
    bool ok = true;
    std::vector<std::string> commands;

    const bool dispatched = hyprmacs::request_client_zorder_marshaled(
        "abc",
        true,
        [&commands](const std::string& command) {
            commands.push_back(command);
            return 0;
        }
    );
    ok &= expect(dispatched, "valid client id should dispatch z-order marshal command");
    ok &= expect(commands == std::vector<std::string>({"dispatch alterzorder top,address:0xabc"}),
                 "marshalled z-order should use alterzorder top with normalized client id");

    commands.clear();
    ok &= expect(
        !hyprmacs::request_client_zorder_marshaled(
            "   ",
            false,
            [&commands](const std::string& command) {
                commands.push_back(command);
                return 0;
            }
        ),
        "blank client id should not dispatch z-order marshal command"
    );
    ok &= expect(commands.empty(), "blank client id should not invoke z-order dispatcher");

    return ok;
}

bool test_visible_rectangles_from_snapshot_preserves_visible_order_and_skips_missing_rectangles() {
    hyprmacs::ManagedWorkspaceLayoutSnapshot snapshot {
        .workspace_id = "1",
        .layout_version = 7,
        .rectangles_by_client_id = {
            {"0xaaa", hyprmacs::ClientRect {.x = 10, .y = 20, .width = 300, .height = 400}},
            {"0xccc", hyprmacs::ClientRect {.x = 50, .y = 60, .width = 700, .height = 800}},
        },
        .visible_client_ids = {"0xccc", "0xbbb", "0xaaa"},
        .hidden_client_ids = {},
        .stacking_order = {"0xaaa", "0xbbb", "0xccc"},
        .selected_client = std::nullopt,
        .input_mode = std::nullopt,
        .managing_emacs_client_id = std::nullopt,
    };

    const auto rectangles = hyprmacs::visible_rectangles_from_snapshot(snapshot);

    bool ok = true;
    ok &= expect(rectangles.size() == 2, "visible rectangle extraction should skip visible clients without rectangles");
    if (rectangles.size() == 2) {
        ok &= expect(rectangles[0].client_id == "0xccc", "visible rectangle extraction should preserve visible order");
        ok &= expect(rectangles[0].x == 50 && rectangles[0].y == 60 && rectangles[0].width == 700 && rectangles[0].height == 800,
                     "visible rectangle extraction should copy first rectangle geometry");
        ok &= expect(rectangles[1].client_id == "0xaaa", "visible rectangle extraction should include later visible rectangle");
        ok &= expect(rectangles[1].x == 10 && rectangles[1].y == 20 && rectangles[1].width == 300 && rectangles[1].height == 400,
                     "visible rectangle extraction should copy second rectangle geometry");
    }
    return ok;
}

bool test_emacs_control_repair_generation_rejects_zero_and_stale_repairs() {
    bool ok = true;

    ok &= expect(!hyprmacs::emacs_control_repair_generation_is_current(0, 0),
                 "zero repair generation should never be current");
    ok &= expect(hyprmacs::emacs_control_repair_generation_is_current(2, 2),
                 "matching non-zero repair generation should be current");
    ok &= expect(!hyprmacs::emacs_control_repair_generation_is_current(3, 2),
                 "stale repair generation should not be current");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;

    ok &= test_recalc_box_pins_managing_emacs_to_work_area();
    ok &= test_recalc_box_places_visible_managed_client_from_snapshot_rectangle();
    ok &= test_recalc_box_ignores_hidden_unmanaged_floating_and_wrong_workspace_targets();
    ok &= test_recalc_visibility_shows_visible_targets_and_hides_hidden_targets();
    ok &= test_recalc_visibility_ignores_emacs_unmanaged_floating_and_wrong_workspace_targets();
    ok &= test_recalc_visibility_restores_hidden_clients_marked_visible_by_snapshot();
    ok &= test_build_workspace_recalc_dispatch_command_normalizes_input();
    ok &= test_request_workspace_recalc_marshaled_dispatches_via_injected_executor();
    ok &= test_build_client_zorder_dispatch_command_normalizes_input();
    ok &= test_request_client_zorder_marshaled_dispatches_via_injected_executor();
    ok &= test_visible_rectangles_from_snapshot_preserves_visible_order_and_skips_missing_rectangles();
    ok &= test_emacs_control_repair_generation_rejects_zero_and_stale_repairs();

    return ok ? 0 : 1;
}
