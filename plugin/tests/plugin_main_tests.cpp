#include "hyprmacs/workspace_manager.hpp"

#include <hyprland/src/helpers/math/Math.hpp>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace hyprmacs {

std::optional<CBox> compute_managed_target_box_for_recalc(const ManagedWorkspaceLayoutSnapshot& snapshot,
                                                          std::string_view target_workspace_id,
                                                          std::string_view target_client_id,
                                                          bool target_floating,
                                                          const CBox& work_area);

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

}  // namespace

int main() {
    bool ok = true;

    ok &= test_recalc_box_pins_managing_emacs_to_work_area();
    ok &= test_recalc_box_places_visible_managed_client_from_snapshot_rectangle();
    ok &= test_recalc_box_ignores_hidden_unmanaged_floating_and_wrong_workspace_targets();

    return ok ? 0 : 1;
}
