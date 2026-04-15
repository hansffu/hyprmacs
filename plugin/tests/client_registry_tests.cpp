#include "hyprmacs/client_classifier.hpp"
#include "hyprmacs/client_registry.hpp"

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

bool test_classifier_rules() {
    bool ok = true;

    hyprmacs::ClientRecord emacs {
        .client_id = "0x1",
        .workspace_id = "1",
        .title = "GNU Emacs",
        .app_id = "emacs",
        .floating = false,
    };
    ok &= expect(!hyprmacs::is_client_eligible(emacs), "emacs client should be excluded");

    hyprmacs::ClientRecord popup {
        .client_id = "0x2",
        .workspace_id = "1",
        .title = "pinentry",
        .app_id = "pinentry",
        .floating = false,
    };
    ok &= expect(!hyprmacs::is_client_eligible(popup), "pinentry client should be excluded");

    hyprmacs::ClientRecord floating {
        .client_id = "0x3",
        .workspace_id = "1",
        .title = "foot",
        .app_id = "foot",
        .floating = true,
    };
    ok &= expect(!hyprmacs::is_client_eligible(floating), "floating client should be excluded");

    hyprmacs::ClientRecord regular {
        .client_id = "0x4",
        .workspace_id = "1",
        .title = "foot",
        .app_id = "foot",
        .floating = false,
    };
    ok &= expect(hyprmacs::is_client_eligible(regular), "regular tiled client should be eligible");

    return ok;
}

bool test_registry_lifecycle() {
    bool ok = true;
    hyprmacs::ClientRegistry registry;

    registry.upsert_open("0xabc", "1", "foot", "shell");
    registry.upsert_open("0xdef", "2", "emacs", "GNU Emacs");

    registry.set_focus("0xabc");
    registry.update_title("0xabc", "shell - repo");
    registry.update_workspace("0xabc", "3");
    registry.set_floating("0xabc", true);

    const auto snapshot = registry.snapshot();
    ok &= expect(snapshot.clients.size() == 2, "expected two clients in snapshot");
    ok &= expect(snapshot.selected_client.has_value(), "selected client should be set");
    ok &= expect(snapshot.selected_client.value() == "0xabc", "selected client should be 0xabc");

    const auto* c1 = registry.find("0xabc");
    ok &= expect(c1 != nullptr, "client 0xabc should exist");
    if (c1 != nullptr) {
        ok &= expect(c1->workspace_id == "3", "workspace should be updated");
        ok &= expect(c1->title == "shell - repo", "title should be updated");
        ok &= expect(c1->floating, "floating should be true");
    }

    registry.erase("0xabc");
    const auto after_erase = registry.snapshot();
    ok &= expect(after_erase.clients.size() == 1, "expected one client after erase");
    ok &= expect(!after_erase.selected_client.has_value(), "selected client should clear after erase");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_classifier_rules();
    ok &= test_registry_lifecycle();
    return ok ? 0 : 1;
}
