#include "hyprmacs/focus_controller.hpp"

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

bool test_focus_client_dispatches_focuswindow_command() {
    std::string captured;
    hyprmacs::FocusController controller([&captured](const std::string& command) {
        captured = command;
        return 0;
    });

    bool ok = true;
    ok &= expect(controller.focus_client("abc"), "focus_client should succeed for valid id");
    ok &= expect(captured == "dispatch focuswindow address:0xabc",
                 "focus command should normalize address to 0x-prefixed form");
    return ok;
}

bool test_focus_client_rejects_empty_id() {
    hyprmacs::FocusController controller([](const std::string&) {
        return 0;
    });
    return expect(!controller.focus_client(""), "empty client id should fail");
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_focus_client_dispatches_focuswindow_command();
    ok &= test_focus_client_rejects_empty_id();
    return ok ? 0 : 1;
}
