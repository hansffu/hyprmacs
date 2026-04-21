#include "hyprmacs/focus_controller.hpp"

#include <utility>

namespace hyprmacs {

FocusController::FocusController(DispatchExecutor dispatch_executor)
    : dispatch_executor_(std::move(dispatch_executor)) {}

bool FocusController::focus_client(const std::string& client_id) {
    if (client_id.empty()) {
        return false;
    }

    std::string normalized = client_id;
    if (normalized.rfind("address:", 0) == 0) {
        normalized = normalized.substr(8);
    }
    if (normalized.rfind("0x", 0) != 0) {
        normalized = "0x" + normalized;
    }

    return dispatch_executor_("dispatch focuswindow address:" + normalized) == 0;
}

}  // namespace hyprmacs
