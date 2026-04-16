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
    std::vector<std::string> args;

    hyprmacs::LayoutApplier applier([&args](const std::string& dispatch_arg) {
        args.push_back(dispatch_arg);
        return 0;
    });

    bool ok = true;

    ok &= expect(applier.hide_client("0xabc", "1"), "hide should succeed");
    ok &= expect(args.size() == 1, "hide should issue one command");
    if (args.size() == 1) {
        ok &= expect(args[0].find("special:hyprmacs-hidden") != std::string::npos,
                     "hide arg should target hidden workspace");
    }

    ok &= expect(applier.show_client("0xabc"), "show should succeed");
    ok &= expect(args.size() == 2, "show should issue second command");
    if (args.size() == 2) {
        ok &= expect(args[1].find("1,address:0xabc") != std::string::npos,
                     "show arg should restore original workspace");
    }

    return ok;
}

bool test_show_without_hidden_state() {
    hyprmacs::LayoutApplier applier([](const std::string&) {
        return 0;
    });
    return expect(!applier.show_client("0xmissing"), "show should fail when no hidden state exists");
}

bool test_hide_does_not_duplicate_commands() {
    int call_count = 0;
    hyprmacs::LayoutApplier applier([&call_count](const std::string&) {
        call_count += 1;
        return 0;
    });

    bool ok = true;
    ok &= expect(applier.hide_client("0xabc", "1"), "first hide should succeed");
    ok &= expect(!applier.hide_client("0xabc", "1"), "second hide should be no-op false");
    ok &= expect(call_count == 1, "only one command should be issued");
    return ok;
}

bool test_id_normalization_across_hide_show() {
    std::vector<std::string> args;
    hyprmacs::LayoutApplier applier([&args](const std::string& dispatch_arg) {
        args.push_back(dispatch_arg);
        return 0;
    });

    bool ok = true;
    ok &= expect(applier.hide_client("55c31e4088a0", "1"), "hide without 0x should succeed");
    ok &= expect(applier.is_hidden("0x55c31e4088a0"), "is_hidden should accept normalized form");
    ok &= expect(applier.show_client("0x55c31e4088a0"), "show with 0x should succeed");
    ok &= expect(args.size() == 2, "normalized hide/show should issue two commands");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_hide_then_restore_roundtrip();
    ok &= test_show_without_hidden_state();
    ok &= test_hide_does_not_duplicate_commands();
    ok &= test_id_normalization_across_hide_show();
    return ok ? 0 : 1;
}
