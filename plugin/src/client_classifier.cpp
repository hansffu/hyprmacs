#include "hyprmacs/client_classifier.hpp"

#include <algorithm>
#include <array>
#include <cctype>
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

bool contains_any(std::string_view value, const std::array<std::string_view, 6>& needles) {
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

    constexpr std::array<std::string_view, 6> kPopupTerms = {
        "pinentry", "dialog", "popup", "tooltip", "menu", "notification",
    };

    return contains_any(app, kPopupTerms) || contains_any(ttl, kPopupTerms);
}

bool is_client_eligible(const ClientRecord& client) {
    if (client.floating) {
        return false;
    }
    if (is_emacs_client(client.app_id, client.title)) {
        return false;
    }
    if (is_popup_or_transient_client(client.app_id, client.title)) {
        return false;
    }
    return true;
}

}  // namespace hyprmacs
