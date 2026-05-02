#include "hyprmacs/client_classifier.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>

namespace hyprmacs {
namespace {

std::string to_lower(std::string_view input) {
    std::string out(input);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool contains_any(std::string_view value, std::initializer_list<std::string_view> needles) {
    for (const auto needle : needles) {
        if (value.find(needle) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool is_emacs_client(std::string_view app_id, std::string_view title) {
    const auto app = to_lower(app_id);
    const auto ttl = to_lower(title);
    if (app == "emacs" || app == "org.gnu.emacs") {
        return true;
    }

    // Fallback only for launchers that omit class/app-id; avoid false positives
    // like a terminal titled "just emacs".
    if (app.empty() || app == "unknown") {
        return ttl.find("gnu emacs") != std::string::npos;
    }
    return false;
}

bool is_popup_or_transient_client(std::string_view app_id, std::string_view title) {
    const auto app = to_lower(app_id);
    const auto ttl = to_lower(title);
    const bool about_dialog_title = ttl == "about" || ttl.rfind("about ", 0) == 0;

    return contains_any(
               app,
               {"pinentry",  "dialog",   "popup",     "tooltip", "menu",     "notification", "portal",
                "chooser",   "polkit",   "auth",      "confirm", "kdialog",  "zenity",       "wofi",
                "rofi",      "dunst",    "notify",    "launcher", "wl-clipboard"}
           )
           || contains_any(
               ttl,
               {"pinentry",       "dialog",         "popup",      "tooltip",     "menu", "notification",
                "open file",      "save file",      "file chooser", "choose file", "confirm", "authentication"}
           )
           || about_dialog_title;
}

bool is_emacs_auxiliary_client(std::string_view app_id, std::string_view title) {
    const auto app = to_lower(app_id);
    const auto ttl = to_lower(title);
    const bool emacs_app = (app == "emacs" || app == "org.gnu.emacs");
    if (!emacs_app) {
        return false;
    }

    return contains_any(ttl, {"minibuf", "posframe", "which-key", "completions", "child frame", "gnu emacs"});
}

bool is_client_eligible(const ClientRecord& client) {
    if (is_emacs_client(client.app_id, client.title)) {
        return false;
    }
    if (is_emacs_auxiliary_client(client.app_id, client.title)) {
        return false;
    }
    if (is_popup_or_transient_client(client.app_id, client.title)) {
        return false;
    }
    return true;
}

}  // namespace hyprmacs
