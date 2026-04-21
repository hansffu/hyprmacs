#include "hyprmacs/client_classifier.hpp"
#include "hyprmacs/client_registry.hpp"

#include <iostream>
#include <optional>
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

    hyprmacs::ClientRecord titled_emacs_but_not_emacs {
        .client_id = "0x5",
        .workspace_id = "1",
        .title = "just emacs",
        .app_id = "foot",
        .floating = false,
    };
    ok &= expect(hyprmacs::is_client_eligible(titled_emacs_but_not_emacs),
                 "non-emacs app should remain eligible even if title mentions emacs");

    hyprmacs::ClientRecord chooser_dialog {
        .client_id = "0x6",
        .workspace_id = "1",
        .title = "Open File Chooser",
        .app_id = "xdg-desktop-portal-gtk",
        .floating = false,
    };
    ok &= expect(!hyprmacs::is_client_eligible(chooser_dialog), "portal chooser dialog should be excluded");

    hyprmacs::ClientRecord auth_prompt {
        .client_id = "0x7",
        .workspace_id = "1",
        .title = "Authentication Required",
        .app_id = "polkit-gnome-authentication-agent-1",
        .floating = false,
    };
    ok &= expect(!hyprmacs::is_client_eligible(auth_prompt), "authentication prompt should be excluded");

    hyprmacs::ClientRecord emacs_child_frame {
        .client_id = "0x8",
        .workspace_id = "1",
        .title = " *Minibuf-1* - GNU Emacs at host",
        .app_id = "org.gnu.emacs",
        .floating = false,
    };
    ok &= expect(!hyprmacs::is_client_eligible(emacs_child_frame), "emacs child frames should be excluded");

    hyprmacs::ClientRecord about_dialog {
        .client_id = "0x9",
        .workspace_id = "1",
        .title = "About PCManFM",
        .app_id = "pcmanfm",
        .floating = false,
    };
    ok &= expect(!hyprmacs::is_client_eligible(about_dialog), "about dialogs should be excluded");

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

bool test_registry_management_reconcile() {
    bool ok = true;
    hyprmacs::ClientRegistry registry;

    registry.upsert_open("0x100", "1", "foot", "one");
    registry.upsert_open("0x200", "2", "foot", "two");
    registry.upsert_open("0x300", "1", "emacs", "GNU Emacs");

    registry.reconcile_management(std::make_optional<std::string>("1"));

    const auto* c1 = registry.find("0x100");
    const auto* c2 = registry.find("0x200");
    const auto* c3 = registry.find("0x300");
    ok &= expect(c1 != nullptr && c1->managed, "eligible client in managed workspace should be managed");
    ok &= expect(c2 != nullptr && !c2->managed, "eligible client outside managed workspace should not be managed");
    ok &= expect(c3 != nullptr && !c3->managed, "ineligible client should not be managed");

    registry.update_workspace("0x200", "1");
    registry.reconcile_management(std::make_optional<std::string>("1"));
    c2 = registry.find("0x200");
    ok &= expect(c2 != nullptr && c2->managed, "eligible moved into managed workspace should be managed");

    registry.set_floating("0x200", true);
    registry.reconcile_management(std::make_optional<std::string>("1"));
    c2 = registry.find("0x200");
    ok &= expect(c2 != nullptr && !c2->managed, "floating client should leave managed set");

    registry.reconcile_management(std::nullopt);
    c1 = registry.find("0x100");
    c2 = registry.find("0x200");
    ok &= expect(c1 != nullptr && !c1->managed, "unmanaged workspace should clear managed flag");
    ok &= expect(c2 != nullptr && !c2->managed, "unmanaged workspace should clear managed flag");

    return ok;
}

bool test_registry_clears_selected_when_client_leaves_managed_set() {
    bool ok = true;
    hyprmacs::ClientRegistry registry;

    registry.upsert_open("0x111", "1", "foot", "one");
    registry.upsert_open("0x222", "1", "foot", "two");
    registry.reconcile_management(std::make_optional<std::string>("1"));
    registry.set_focus("0x111");

    registry.set_floating("0x111", true);
    registry.reconcile_management(std::make_optional<std::string>("1"));

    const auto snapshot = registry.snapshot();
    ok &= expect(!snapshot.selected_client.has_value(),
                 "selected client should clear when it becomes floating and leaves managed set");
    const auto* c1 = registry.find("0x111");
    ok &= expect(c1 != nullptr && !c1->managed, "floating selected client should no longer be managed");

    return ok;
}

bool test_registry_normalizes_client_ids() {
    bool ok = true;
    hyprmacs::ClientRegistry registry;

    registry.upsert_open("55c31e4088a0", "1", "foot", "one");
    registry.upsert_open("0x55c31e4088a0", "1", "foot", "one-updated");
    registry.set_focus("55c31e4088a0");
    registry.set_floating("0x55c31e4088a0", true);

    const auto snapshot = registry.snapshot();
    ok &= expect(snapshot.clients.size() == 1, "normalized ids should deduplicate into one client");
    if (snapshot.clients.size() == 1) {
        ok &= expect(snapshot.clients[0].client_id == "0x55c31e4088a0", "stored id should be 0x-prefixed");
        ok &= expect(snapshot.clients[0].floating, "floating update should apply across normalized forms");
    }
    ok &= expect(snapshot.selected_client.has_value(), "selected client should be set");
    if (snapshot.selected_client.has_value()) {
        ok &= expect(*snapshot.selected_client == "0x55c31e4088a0", "selected id should be normalized");
    }

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_classifier_rules();
    ok &= test_registry_lifecycle();
    ok &= test_registry_management_reconcile();
    ok &= test_registry_clears_selected_when_client_leaves_managed_set();
    ok &= test_registry_normalizes_client_ids();
    return ok ? 0 : 1;
}
