#include "hyprmacs/focus_controller.hpp"

#include <utility>

namespace hyprmacs {

FocusController::FocusController(DispatchExecutor dispatch_executor)
    : dispatch_executor_(std::move(dispatch_executor)) {}

std::string FocusController::normalize_client_id(const std::string& client_id) const {
    std::string normalized = client_id;
    if (normalized.rfind("address:", 0) == 0) {
        normalized = normalized.substr(8);
    }
    if (normalized.rfind("0x", 0) != 0) {
        normalized = "0x" + normalized;
    }
    return normalized;
}

bool FocusController::focus_client(const std::string& client_id) {
    if (client_id.empty()) {
        return false;
    }

    const std::string normalized = normalize_client_id(client_id);
    return dispatch_executor_("dispatch focuswindow address:" + normalized) == 0;
}

bool FocusController::alter_zorder(const std::string& client_id, bool top) {
    if (client_id.empty()) {
        return false;
    }

    const std::string normalized = normalize_client_id(client_id);
    return dispatch_executor_(std::string("dispatch alterzorder ") + (top ? "top" : "bottom") + ",address:" + normalized) == 0;
}

}  // namespace hyprmacs
