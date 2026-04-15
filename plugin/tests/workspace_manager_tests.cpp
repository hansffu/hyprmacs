#include "hyprmacs/workspace_manager.hpp"

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

}  // namespace

int main() {
    bool ok = true;
    ok &= test_parse_event_frame();
    ok &= test_tracked_event_names();
    return ok ? 0 : 1;
}
