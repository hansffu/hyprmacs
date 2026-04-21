#pragma once

#include <string_view>

#include "hyprmacs/client_registry.hpp"

namespace hyprmacs {

bool is_emacs_client(std::string_view app_id, std::string_view title);
bool is_popup_or_transient_client(std::string_view app_id, std::string_view title);
bool is_client_eligible(const ClientRecord& client);

}  // namespace hyprmacs
