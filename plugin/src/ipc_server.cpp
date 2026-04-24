#include "hyprmacs/ipc_server.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace hyprmacs {
namespace {

constexpr int kDefaultStateNotifyDebounceMs = 30;
constexpr int kMaxStateNotifyDebounceMs = 1000;

std::string now_rfc3339() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm {};
    gmtime_r(&t, &tm);

    char buffer[32] {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string payload_for_workspace_managed(bool controller_connected) {
    std::ostringstream out;
    out << "{";
    out << "\"managed\":true,";
    out << "\"controller_connected\":" << (controller_connected ? "true" : "false");
    out << "}";
    return out.str();
}

std::string payload_for_workspace_unmanaged(std::string_view reason) {
    std::ostringstream out;
    out << "{";
    out << "\"managed\":false,";
    out << "\"reason\":\"" << reason << "\"";
    out << "}";
    return out.str();
}

std::string payload_for_debug_hide_show_result(std::string_view client_id, bool ok, std::string_view action) {
    std::ostringstream out;
    out << "{";
    out << "\"client_id\":\"" << client_id << "\",";
    out << "\"action\":\"" << action << "\",";
    out << "\"ok\":" << (ok ? "true" : "false");
    out << "}";
    return out.str();
}

std::string payload_for_layout_applied(std::string_view selected_client, bool ok) {
    std::ostringstream out;
    out << "{";
    out << "\"selected_client\":\"" << selected_client << "\",";
    out << "\"ok\":" << (ok ? "true" : "false");
    out << "}";
    return out.str();
}

std::string payload_for_mode_changed(std::string_view mode, bool ok) {
    std::ostringstream out;
    out << "{";
    out << "\"mode\":\"" << mode << "\",";
    out << "\"ok\":" << (ok ? "true" : "false");
    out << "}";
    return out.str();
}

std::string payload_for_client_transition(std::string_view client_id, bool floating) {
    std::ostringstream out;
    out << "{";
    out << "\"client_id\":\"" << client_id << "\",";
    out << "\"floating\":" << (floating ? "true" : "false");
    out << "}";
    return out.str();
}

std::string payload_for_client_floated(std::string_view client_id, bool ok) {
    std::ostringstream out;
    out << "{";
    out << "\"client_id\":\"" << client_id << "\",";
    out << "\"floating\":true,";
    out << "\"ok\":" << (ok ? "true" : "false");
    out << "}";
    return out.str();
}

std::string escape_payload_json(std::string_view value) {
    std::ostringstream out;
    for (const char ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (uch < 0x20) {
                    out << "\\u"
                        << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(uch)
                        << std::dec << std::setfill(' ');
                } else {
                    out << ch;
                }
                break;
        }
    }
    return out.str();
}

std::string payload_for_summon_candidates(
    const std::vector<SummonCandidate>& candidates,
    std::optional<std::string_view> request_id = std::nullopt
) {
    std::ostringstream out;
    out << "{";
    if (request_id.has_value()) {
        out << "\"request_id\":\"" << escape_payload_json(*request_id) << "\",";
    }
    out << "\"candidates\":[";
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << "{";
        out << "\"client_id\":\"" << escape_payload_json(candidates[i].client_id) << "\",";
        out << "\"workspace_id\":\"" << escape_payload_json(candidates[i].workspace_id) << "\",";
        out << "\"app_id\":\"" << escape_payload_json(candidates[i].app_id) << "\",";
        out << "\"title\":\"" << escape_payload_json(candidates[i].title) << "\"";
        out << "}";
    }
    out << "]}";
    return out.str();
}

std::string payload_for_client_summoned(std::string_view client_id, bool ok) {
    std::ostringstream out;
    out << "{";
    out << "\"client_id\":\"" << escape_payload_json(client_id) << "\",";
    out << "\"ok\":" << (ok ? "true" : "false");
    out << "}";
    return out.str();
}

std::string payload_for_focus_request(std::string_view client_id) {
    std::ostringstream out;
    out << "{";
    out << "\"client_id\":\"" << escape_payload_json(client_id) << "\"";
    out << "}";
    return out.str();
}

std::string payload_for_protocol_error(std::string_view code, std::string_view message, std::string_view detail = "") {
    std::ostringstream out;
    out << "{";
    out << "\"code\":\"" << code << "\",";
    out << "\"message\":\"" << message << "\"";
    if (!detail.empty()) {
        out << ",\"detail\":\"" << detail << "\"";
    }
    out << "}";
    return out.str();
}

ProtocolMessage make_message(std::string_view type, const WorkspaceId& workspace_id, std::string payload_json) {
    return ProtocolMessage {
        .version = 1,
        .type = std::string(type),
        .workspace_id = workspace_id,
        .timestamp = now_rfc3339(),
        .payload_json = std::move(payload_json),
    };
}

std::optional<std::string> parse_client_id_from_payload(std::string_view payload_json) {
    const std::string key = "\"client_id\"";
    const size_t key_pos = payload_json.find(key);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t colon_pos = payload_json.find(':', key_pos + key.size());
    if (colon_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t first_quote = payload_json.find('"', colon_pos + 1);
    if (first_quote == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t second_quote = payload_json.find('"', first_quote + 1);
    if (second_quote == std::string_view::npos) {
        return std::nullopt;
    }

    return std::string(payload_json.substr(first_quote + 1, second_quote - first_quote - 1));
}

std::optional<unsigned int> parse_hex4(std::string_view text, size_t pos) {
    if (pos + 4 > text.size()) {
        return std::nullopt;
    }

    unsigned int value = 0;
    for (size_t i = 0; i < 4; ++i) {
        const char ch = text[pos + i];
        unsigned int digit = 0;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<unsigned int>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = static_cast<unsigned int>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            digit = static_cast<unsigned int>(ch - 'A' + 10);
        } else {
            return std::nullopt;
        }
        value = (value << 4U) | digit;
    }
    return value;
}

void append_utf8(std::string* out, unsigned int codepoint) {
    if (codepoint <= 0x7FU) {
        out->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFU) {
        out->push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
        out->push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFFU) {
        out->push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
        out->push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        out->push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    } else {
        out->push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
        out->push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
        out->push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
        out->push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
}

std::optional<std::string> parse_json_string_literal(std::string_view text, size_t* pos) {
    if (*pos >= text.size() || text[*pos] != '"') {
        return std::nullopt;
    }
    ++(*pos);

    std::string out;
    while (*pos < text.size()) {
        const char ch = text[*pos];
        ++(*pos);
        if (ch == '"') {
            return out;
        }
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (*pos >= text.size()) {
            return std::nullopt;
        }
        const char escaped = text[*pos];
        ++(*pos);
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escaped);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                const auto high = parse_hex4(text, *pos);
                if (!high) {
                    return std::nullopt;
                }
                *pos += 4;

                unsigned int codepoint = *high;
                if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
                    if (*pos + 6 > text.size() || text[*pos] != '\\' || text[*pos + 1] != 'u') {
                        return std::nullopt;
                    }
                    *pos += 2;
                    const auto low = parse_hex4(text, *pos);
                    if (!low || *low < 0xDC00U || *low > 0xDFFFU) {
                        return std::nullopt;
                    }
                    *pos += 4;
                    codepoint = 0x10000U + (((codepoint - 0xD800U) << 10U) | (*low - 0xDC00U));
                } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
                    return std::nullopt;
                }
                append_utf8(&out, codepoint);
                break;
            }
            default:
                return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> parse_string_field_from_payload(std::string_view payload_json, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    const size_t key_pos = payload_json.find(token);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t colon_pos = payload_json.find(':', key_pos + token.size());
    if (colon_pos == std::string_view::npos) {
        return std::nullopt;
    }

    size_t value_start = colon_pos + 1;
    while (value_start < payload_json.size() &&
           std::isspace(static_cast<unsigned char>(payload_json[value_start])) != 0) {
        ++value_start;
    }
    if (value_start >= payload_json.size() || payload_json[value_start] != '"') {
        return std::nullopt;
    }

    return parse_json_string_literal(payload_json, &value_start);
}

std::optional<bool> parse_bool_field_from_payload(std::string_view payload_json, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    const size_t key_pos = payload_json.find(token);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t colon_pos = payload_json.find(':', key_pos + token.size());
    if (colon_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t value_start = payload_json.find_first_not_of(" \t\r\n", colon_pos + 1);
    if (value_start == std::string_view::npos) {
        return std::nullopt;
    }
    if (payload_json.compare(value_start, 4, "true") == 0) {
        return true;
    }
    if (payload_json.compare(value_start, 5, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

std::optional<int> parse_int_field_from_payload(std::string_view payload_json, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    const size_t key_pos = payload_json.find(token);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t colon_pos = payload_json.find(':', key_pos + token.size());
    if (colon_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t value_start = payload_json.find_first_not_of(" \t\r\n", colon_pos + 1);
    if (value_start == std::string_view::npos) {
        return std::nullopt;
    }

    size_t value_end = value_start;
    if (payload_json[value_end] == '-') {
        ++value_end;
    }
    while (value_end < payload_json.size() && std::isdigit(static_cast<unsigned char>(payload_json[value_end])) != 0) {
        ++value_end;
    }
    if (value_end == value_start || (value_end == value_start + 1 && payload_json[value_start] == '-')) {
        return std::nullopt;
    }

    try {
        return std::stoi(std::string(payload_json.substr(value_start, value_end - value_start)));
    } catch (...) {
        return std::nullopt;
    }
}

void skip_ws(std::string_view text, size_t* pos) {
    while (*pos < text.size() && std::isspace(static_cast<unsigned char>(text[*pos])) != 0) {
        ++(*pos);
    }
}

std::optional<std::vector<std::string>> parse_string_array_field_from_payload(std::string_view payload_json, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    const size_t key_pos = payload_json.find(token);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }

    size_t pos = payload_json.find(':', key_pos + token.size());
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    ++pos;
    skip_ws(payload_json, &pos);
    if (pos >= payload_json.size() || payload_json[pos] != '[') {
        return std::nullopt;
    }
    ++pos;

    std::vector<std::string> out;
    while (true) {
        skip_ws(payload_json, &pos);
        if (pos >= payload_json.size()) {
            return std::nullopt;
        }
        if (payload_json[pos] == ']') {
            ++pos;
            break;
        }
        if (payload_json[pos] != '"') {
            return std::nullopt;
        }
        ++pos;
        const size_t start = pos;
        const size_t end = payload_json.find('"', start);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        out.emplace_back(payload_json.substr(start, end - start));
        pos = end + 1;

        skip_ws(payload_json, &pos);
        if (pos >= payload_json.size()) {
            return std::nullopt;
        }
        if (payload_json[pos] == ',') {
            ++pos;
            continue;
        }
        if (payload_json[pos] == ']') {
            ++pos;
            break;
        }
        return std::nullopt;
    }

    return out;
}

std::optional<std::vector<LayoutRectangle>> parse_rectangles_field_from_payload(std::string_view payload_json, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    const size_t key_pos = payload_json.find(token);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }

    size_t pos = payload_json.find(':', key_pos + token.size());
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    ++pos;
    skip_ws(payload_json, &pos);
    if (pos >= payload_json.size() || payload_json[pos] != '[') {
        return std::nullopt;
    }
    ++pos;

    std::vector<LayoutRectangle> out;
    while (true) {
        skip_ws(payload_json, &pos);
        if (pos >= payload_json.size()) {
            return std::nullopt;
        }
        if (payload_json[pos] == ']') {
            ++pos;
            break;
        }
        if (payload_json[pos] != '{') {
            return std::nullopt;
        }

        const size_t object_start = pos;
        int depth = 0;
        while (pos < payload_json.size()) {
            if (payload_json[pos] == '{') {
                ++depth;
            } else if (payload_json[pos] == '}') {
                --depth;
                if (depth == 0) {
                    ++pos;
                    break;
                }
            }
            ++pos;
        }
        if (depth != 0) {
            return std::nullopt;
        }

        const std::string object_json(payload_json.substr(object_start, pos - object_start));
        const auto client_id = parse_string_field_from_payload(object_json, "client_id");
        const auto x = parse_int_field_from_payload(object_json, "x");
        const auto y = parse_int_field_from_payload(object_json, "y");
        const auto width = parse_int_field_from_payload(object_json, "width");
        const auto height = parse_int_field_from_payload(object_json, "height");
        if (!client_id.has_value() || !x.has_value() || !y.has_value() || !width.has_value() || !height.has_value()) {
            return std::nullopt;
        }

        out.push_back(LayoutRectangle {
            .client_id = *client_id,
            .x = *x,
            .y = *y,
            .width = *width,
            .height = *height,
        });

        skip_ws(payload_json, &pos);
        if (pos >= payload_json.size()) {
            return std::nullopt;
        }
        if (payload_json[pos] == ',') {
            ++pos;
            continue;
        }
        if (payload_json[pos] == ']') {
            ++pos;
            break;
        }
        return std::nullopt;
    }

    return out;
}

std::optional<std::vector<LayoutRectangle>> build_visible_rectangles_for_set_layout(
    const std::vector<std::string>& visible_clients,
    const std::vector<LayoutRectangle>& rectangles,
    std::string* error_out
) {
    std::unordered_map<std::string, LayoutRectangle> rectangles_by_client_id;
    rectangles_by_client_id.reserve(rectangles.size());

    for (const auto& rectangle : rectangles) {
        const auto [it, inserted] = rectangles_by_client_id.emplace(rectangle.client_id, rectangle);
        if (!inserted) {
            if (error_out != nullptr) {
                *error_out = "set-layout has multiple rectangles for client " + rectangle.client_id;
            }
            return std::nullopt;
        }
    }

    std::vector<LayoutRectangle> visible_rectangles;
    visible_rectangles.reserve(visible_clients.size());
    for (const auto& visible_client : visible_clients) {
        const auto it = rectangles_by_client_id.find(visible_client);
        if (it == rectangles_by_client_id.end()) {
            if (error_out != nullptr) {
                *error_out = "set-layout missing rectangle for visible client " + visible_client;
            }
            return std::nullopt;
        }
        visible_rectangles.push_back(it->second);
    }

    if (rectangles_by_client_id.size() != visible_clients.size()) {
        for (const auto& [client_id, _] : rectangles_by_client_id) {
            if (std::find(visible_clients.begin(), visible_clients.end(), client_id) == visible_clients.end()) {
                if (error_out != nullptr) {
                    *error_out = "set-layout has rectangle for non-visible client " + client_id;
                }
                return std::nullopt;
            }
        }
    }

    return visible_rectangles;
}

std::unordered_set<std::string> managed_and_eligible_client_ids(const StateDumpPayload& state) {
    std::unordered_set<std::string> managed_ids;
    managed_ids.reserve(state.managed_clients.size());
    for (const auto& client_id : state.managed_clients) {
        managed_ids.insert(client_id);
    }

    std::unordered_set<std::string> allowed_ids;
    allowed_ids.reserve(state.eligible_clients.size());
    for (const auto& client : state.eligible_clients) {
        if (managed_ids.find(client.client_id) != managed_ids.end()) {
            allowed_ids.insert(client.client_id);
        }
    }

    return allowed_ids;
}

bool validate_unique_client_list(std::string_view list_name, const std::vector<std::string>& client_ids, std::string* error_out) {
    std::unordered_set<std::string> seen;
    seen.reserve(client_ids.size());
    for (const auto& client_id : client_ids) {
        if (!seen.insert(client_id).second) {
            if (error_out != nullptr) {
                *error_out = "set-layout has duplicate client_id in " + std::string(list_name) + ": " + client_id;
            }
            return false;
        }
    }
    return true;
}

bool validate_client_membership(std::string_view list_name, const std::vector<std::string>& client_ids,
                                const std::unordered_set<std::string>& allowed_ids, std::string* error_out) {
    for (const auto& client_id : client_ids) {
        if (allowed_ids.find(client_id) == allowed_ids.end()) {
            if (error_out != nullptr) {
                *error_out = "set-layout references unmanaged or ineligible client " + client_id + " in "
                             + std::string(list_name);
            }
            return false;
        }
    }
    return true;
}

bool validate_stacking_order_is_visible_permutation(const std::vector<std::string>& visible_clients,
                                                    const std::vector<std::string>& stacking_order,
                                                    std::string* error_out) {
    if (stacking_order.size() != visible_clients.size()) {
        if (error_out != nullptr) {
            *error_out = "set-layout stacking_order must list the same clients as visible_clients";
        }
        return false;
    }

    std::unordered_set<std::string> visible_set(visible_clients.begin(), visible_clients.end());
    std::unordered_set<std::string> stacking_set;
    stacking_set.reserve(stacking_order.size());
    for (const auto& client_id : stacking_order) {
        if (!stacking_set.insert(client_id).second) {
            if (error_out != nullptr) {
                *error_out = "set-layout has duplicate client_id in stacking_order: " + client_id;
            }
            return false;
        }
        if (visible_set.find(client_id) == visible_set.end()) {
            if (error_out != nullptr) {
                *error_out = "set-layout stacking_order references non-visible client " + client_id;
            }
            return false;
        }
    }

    if (stacking_set.size() != visible_set.size()) {
        if (error_out != nullptr) {
            *error_out = "set-layout stacking_order must be a unique ordering of visible_clients";
        }
        return false;
    }

    return true;
}

std::optional<InputMode> parse_input_mode_field_from_payload(std::string_view payload_json, std::string_view key) {
    const auto mode = parse_string_field_from_payload(payload_json, key);
    if (!mode.has_value()) {
        return std::nullopt;
    }
    if (*mode == "emacs-control") {
        return InputMode::kEmacsControl;
    }
    if (*mode == "client-control") {
        return InputMode::kClientControl;
    }
    return std::nullopt;
}

void apply_managed_overlay_commands(LayoutApplier& layout_applier,
                                    WorkspaceManager* workspace_manager,
                                    const WorkspaceId& workspace_id,
                                    const std::vector<std::string>& stacking_order,
                                    const std::vector<LayoutRectangle>& visible_rectangles) {
    std::unordered_map<std::string, LayoutRectangle> rectangles_by_client_id;
    rectangles_by_client_id.reserve(visible_rectangles.size());
    for (const auto& rectangle : visible_rectangles) {
        rectangles_by_client_id.emplace(rectangle.client_id, rectangle);
    }

    for (const auto& client_id : stacking_order) {
        const auto it = rectangles_by_client_id.find(client_id);
        if (it == rectangles_by_client_id.end()) {
            continue;
        }

        if (workspace_manager != nullptr) {
            workspace_manager->note_overlay_float_request(workspace_id, client_id);
        }
        if (!layout_applier.ensure_client_floating(client_id)) {
            std::cerr << "[hyprmacs] set-layout setfloating failed workspace=" << workspace_id
                      << " client=" << client_id << '\n';
        }
        if (!layout_applier.apply_floating_geometry(it->second)) {
            std::cerr << "[hyprmacs] set-layout floating geometry failed workspace=" << workspace_id
                      << " client=" << client_id << '\n';
        }
    }

    for (auto it = stacking_order.rbegin(); it != stacking_order.rend(); ++it) {
        if (rectangles_by_client_id.find(*it) == rectangles_by_client_id.end()) {
            continue;
        }
        if (!layout_applier.lower_client_zorder(*it)) {
            std::cerr << "[hyprmacs] set-layout managed z-order lower failed workspace=" << workspace_id
                      << " client=" << *it << '\n';
        }
    }
}

void apply_managed_overlay_zorder_only(LayoutApplier& layout_applier,
                                       const WorkspaceId& workspace_id,
                                       const std::vector<std::string>& stacking_order,
                                       const std::unordered_set<std::string>& visible_client_ids) {
    for (auto it = stacking_order.rbegin(); it != stacking_order.rend(); ++it) {
        if (visible_client_ids.find(*it) == visible_client_ids.end()) {
            continue;
        }
        if (!layout_applier.lower_client_zorder(*it)) {
            std::cerr << "[hyprmacs] state-change managed z-order lower failed workspace=" << workspace_id
                      << " client=" << *it << '\n';
        }
    }
}

}  // namespace

int normalize_state_notify_debounce_ms(std::optional<int> configured_value, bool* used_default_out, bool* clamped_out) {
    bool used_default = false;
    bool clamped = false;
    int debounce_ms = kDefaultStateNotifyDebounceMs;

    if (!configured_value.has_value()) {
        used_default = true;
    } else {
        debounce_ms = *configured_value;
        if (debounce_ms < 0) {
            debounce_ms = 0;
            clamped = true;
        } else if (debounce_ms > kMaxStateNotifyDebounceMs) {
            debounce_ms = kMaxStateNotifyDebounceMs;
            clamped = true;
        }
    }

    if (used_default_out != nullptr) {
        *used_default_out = used_default;
    }
    if (clamped_out != nullptr) {
        *clamped_out = clamped;
    }
    return debounce_ms;
}

std::optional<std::string> default_ipc_socket_path() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    const char* instance = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (runtime_dir == nullptr || *runtime_dir == '\0' || instance == nullptr || *instance == '\0') {
        return std::nullopt;
    }

    std::string path = runtime_dir;
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    path += "hypr/";
    path += instance;
    path += "/hyprmacs-v1.sock";
    return path;
}

std::vector<ProtocolMessage> route_command_for_tests(
    const ProtocolMessage& incoming,
    WorkspaceManager& workspace_manager,
    LayoutApplier& layout_applier,
    FocusController* focus_controller,
    RecalcRequester recalc_requester
) {
    std::vector<ProtocolMessage> out;

    if (incoming.type == "manage-workspace") {
        workspace_manager.manage_workspace(incoming.workspace_id);
        const auto state = workspace_manager.build_state_dump(incoming.workspace_id);
        for (const auto& managed_client : state.managed_clients) {
            (void)layout_applier.hide_client(managed_client, incoming.workspace_id);
        }
        if (focus_controller != nullptr) {
            const auto emacs_client = workspace_manager.emacs_client(incoming.workspace_id);
            if (emacs_client.has_value()) {
                (void)focus_controller->focus_client(*emacs_client);
            }
        }
        out.push_back(make_message("workspace-managed", incoming.workspace_id, payload_for_workspace_managed(true)));
        out.push_back(make_message("state-dump", incoming.workspace_id, serialize_state_dump_payload(state)));
        return out;
    }

    if (incoming.type == "unmanage-workspace") {
        workspace_manager.unmanage_workspace(incoming.workspace_id);
        out.push_back(make_message("workspace-unmanaged", incoming.workspace_id, payload_for_workspace_unmanaged("user-request")));
        out.push_back(make_message(
            "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
        ));
        return out;
    }

    if (incoming.type == "float-managed-client") {
        const auto client_id = parse_string_field_from_payload(incoming.payload_json, "client_id");
        if (!client_id.has_value()) {
            out.push_back(make_message(
                "protocol-error",
                incoming.workspace_id,
                payload_for_protocol_error("invalid-payload", "float-managed-client missing client_id")
            ));
            return out;
        }

        bool ok = workspace_manager.can_float_managed_client(incoming.workspace_id, *client_id);
        const bool was_hidden = ok && layout_applier.is_hidden(*client_id);
        bool restored_hidden_client = false;
        if (ok) {
            ok = layout_applier.show_client(*client_id);
            restored_hidden_client = ok && was_hidden;
        }
        if (ok) {
            ok = layout_applier.ensure_client_floating(*client_id);
            if (!ok && restored_hidden_client) {
                (void)layout_applier.hide_client(*client_id, incoming.workspace_id);
            }
        }
        if (ok) {
            ok = workspace_manager.float_managed_client(incoming.workspace_id, *client_id);
            if (!ok && restored_hidden_client) {
                (void)layout_applier.hide_client(*client_id, incoming.workspace_id);
            }
        }
        out.push_back(make_message("client-floated", incoming.workspace_id, payload_for_client_floated(*client_id, ok)));
        out.push_back(make_message(
            "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
        ));
        return out;
    }

    if (incoming.type == "list-summon-candidates") {
        const auto request_id = parse_string_field_from_payload(incoming.payload_json, "request_id");
        out.push_back(make_message(
            "summon-candidates",
            incoming.workspace_id,
            payload_for_summon_candidates(
                workspace_manager.summon_candidates(incoming.workspace_id),
                request_id ? std::optional<std::string_view>(*request_id) : std::nullopt
            )
        ));
        return out;
    }

    if (incoming.type == "summon-client") {
        const auto client_id = parse_string_field_from_payload(incoming.payload_json, "client_id");
        if (!client_id.has_value()) {
            out.push_back(make_message(
                "protocol-error",
                incoming.workspace_id,
                payload_for_protocol_error("invalid-payload", "summon-client missing client_id")
            ));
            return out;
        }

        const bool ok = workspace_manager.summon_client(incoming.workspace_id, *client_id);
        out.push_back(make_message("client-summoned", incoming.workspace_id, payload_for_client_summoned(*client_id, ok)));
        out.push_back(make_message(
            "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
        ));
        return out;
    }

    if (incoming.type == "request-state") {
        workspace_manager.refresh_workspace_floating_state_from_query(incoming.workspace_id);
        out.push_back(make_message(
            "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
        ));
        return out;
    }

    if (incoming.type == "debug-hide-client") {
        const auto client_id = parse_client_id_from_payload(incoming.payload_json);
        if (client_id.has_value()) {
            const bool ok = layout_applier.hide_client(*client_id, incoming.workspace_id);
            out.push_back(make_message(
                "debug-hide-client-result", incoming.workspace_id, payload_for_debug_hide_show_result(*client_id, ok, "hide")
            ));
            out.push_back(make_message(
                "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
            ));
        }
        return out;
    }

    if (incoming.type == "debug-show-client") {
        const auto client_id = parse_client_id_from_payload(incoming.payload_json);
        if (client_id.has_value()) {
            const bool ok = layout_applier.show_client(*client_id);
            out.push_back(make_message(
                "debug-show-client-result", incoming.workspace_id, payload_for_debug_hide_show_result(*client_id, ok, "show")
            ));
            out.push_back(make_message(
                "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
            ));
        }
        return out;
    }

    if (incoming.type == "set-selected-client") {
        const auto client_id = parse_client_id_from_payload(incoming.payload_json);
        if (client_id.has_value()) {
            const bool ok = workspace_manager.set_selected_client(incoming.workspace_id, *client_id);
            if (ok) {
                const auto state = workspace_manager.build_state_dump(incoming.workspace_id);
                bool selected_can_be_shown = false;
                if (state.selected_client.has_value()) {
                    const auto snapshot = workspace_manager.managed_layout_snapshot(incoming.workspace_id);
                    if (snapshot.has_value()) {
                        const auto visible_it = std::find(
                            snapshot->visible_client_ids.begin(),
                            snapshot->visible_client_ids.end(),
                            *state.selected_client
                        );
                        if (visible_it != snapshot->visible_client_ids.end()) {
                            selected_can_be_shown =
                                snapshot->rectangles_by_client_id.find(*state.selected_client) !=
                                snapshot->rectangles_by_client_id.end();
                        }
                    }
                }
                if (state.selected_client.has_value()) {
                    for (const auto& managed_client : state.managed_clients) {
                        if (managed_client == *state.selected_client && selected_can_be_shown) {
                            if (layout_applier.show_client(managed_client)) {
                                workspace_manager.note_internal_focus_request(incoming.workspace_id, managed_client);
                            }
                        } else {
                            (void)layout_applier.hide_client(managed_client, incoming.workspace_id);
                        }
                    }
                }
            }

            out.push_back(make_message("layout-applied", incoming.workspace_id, payload_for_layout_applied(*client_id, ok)));
            out.push_back(make_message(
                "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
            ));
        }
        return out;
    }

    if (incoming.type == "set-input-mode") {
        const auto mode = parse_input_mode_field_from_payload(incoming.payload_json, "mode");
        bool ok = false;
        std::string wire_mode = "unknown";
        if (mode.has_value()) {
            ok = workspace_manager.set_input_mode(incoming.workspace_id, *mode);
            wire_mode = (*mode == InputMode::kClientControl) ? "client-control" : "emacs-control";
            if (ok && focus_controller != nullptr) {
                if (*mode == InputMode::kClientControl) {
                    auto selected = workspace_manager.selected_managed_client(incoming.workspace_id);
                    if (!selected.has_value()) {
                        const auto state = workspace_manager.build_state_dump(incoming.workspace_id);
                        if (!state.managed_clients.empty()) {
                            selected = state.managed_clients.front();
                            (void)workspace_manager.set_selected_client(incoming.workspace_id, *selected);
                        }
                    }
                    if (selected.has_value()) {
                        if (focus_controller->focus_client(*selected)) {
                            workspace_manager.note_internal_focus_request(incoming.workspace_id, *selected);
                        }
                    }
                } else if (*mode == InputMode::kEmacsControl) {
                    const auto emacs_client = workspace_manager.emacs_client(incoming.workspace_id);
                    if (emacs_client.has_value()) {
                        (void)focus_controller->focus_client(*emacs_client);
                        if (!focus_controller->alter_zorder(*emacs_client, false)) {
                            std::cerr << "[hyprmacs] set-input-mode emacs-control z-order lower failed workspace="
                                      << incoming.workspace_id << " client=" << *emacs_client << '\n';
                        }
                    }
                }
            }
            if (ok && *mode == InputMode::kEmacsControl) {
                const auto snapshot = workspace_manager.managed_layout_snapshot(incoming.workspace_id);
                if (snapshot.has_value()) {
                    std::vector<LayoutRectangle> visible_rectangles;
                    visible_rectangles.reserve(snapshot->visible_client_ids.size());
                    for (const auto& client_id : snapshot->visible_client_ids) {
                        const auto rect_it = snapshot->rectangles_by_client_id.find(client_id);
                        if (rect_it == snapshot->rectangles_by_client_id.end()) {
                            continue;
                        }
                        visible_rectangles.push_back(LayoutRectangle {
                            .client_id = client_id,
                            .x = rect_it->second.x,
                            .y = rect_it->second.y,
                            .width = rect_it->second.width,
                            .height = rect_it->second.height,
                        });
                    }
                    apply_managed_overlay_commands(
                        layout_applier,
                        &workspace_manager,
                        incoming.workspace_id,
                        snapshot->stacking_order,
                        visible_rectangles
                    );
                }
            }
        }
        out.push_back(make_message("mode-changed", incoming.workspace_id, payload_for_mode_changed(wire_mode, ok)));
        out.push_back(make_message(
            "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
        ));
        return out;
    }

    if (incoming.type == "set-layout") {
        bool ok = true;
        std::string error;

        const auto selected_client = parse_string_field_from_payload(incoming.payload_json, "selected_client");
        const auto input_mode = parse_input_mode_field_from_payload(incoming.payload_json, "input_mode");
        const auto visible_clients_opt = parse_string_array_field_from_payload(incoming.payload_json, "visible_clients");
        const auto hidden_clients_opt = parse_string_array_field_from_payload(incoming.payload_json, "hidden_clients");
        const auto rectangles_opt = parse_rectangles_field_from_payload(incoming.payload_json, "rectangles");
        const auto stacking_order_opt = parse_string_array_field_from_payload(incoming.payload_json, "stacking_order");

        if (!visible_clients_opt.has_value() || !hidden_clients_opt.has_value() || !rectangles_opt.has_value() ||
            !stacking_order_opt.has_value()) {
            ok = false;
            error = "set-layout missing required fields";
        }

        if (ok) {
            const auto state = workspace_manager.build_state_dump(incoming.workspace_id);
            const auto allowed_ids = managed_and_eligible_client_ids(state);

            ok = validate_unique_client_list("visible_clients", *visible_clients_opt, &error) &&
                 validate_unique_client_list("hidden_clients", *hidden_clients_opt, &error) &&
                 validate_unique_client_list("stacking_order", *stacking_order_opt, &error) &&
                 validate_client_membership("visible_clients", *visible_clients_opt, allowed_ids, &error) &&
                 validate_client_membership("hidden_clients", *hidden_clients_opt, allowed_ids, &error) &&
                 validate_client_membership("stacking_order", *stacking_order_opt, allowed_ids, &error);

            if (ok) {
                std::unordered_set<std::string> visible_ids(visible_clients_opt->begin(), visible_clients_opt->end());
                std::unordered_set<std::string> hidden_ids(hidden_clients_opt->begin(), hidden_clients_opt->end());
                for (const auto& client_id : visible_ids) {
                    if (hidden_ids.find(client_id) != hidden_ids.end()) {
                        ok = false;
                        error = "set-layout visible_clients and hidden_clients overlap at " + client_id;
                        break;
                    }
                }
            }

            if (ok) {
                ok = validate_stacking_order_is_visible_permutation(*visible_clients_opt, *stacking_order_opt, &error);
            }

            std::optional<std::vector<LayoutRectangle>> visible_rectangles;
            if (ok) {
                visible_rectangles = build_visible_rectangles_for_set_layout(*visible_clients_opt, *rectangles_opt, &error);
                if (!visible_rectangles.has_value()) {
                    ok = false;
                } else {
                    std::string overlap_error;
                    if (!LayoutApplier::validate_non_overlapping(*visible_rectangles, &overlap_error)) {
                        ok = false;
                        error = overlap_error;
                    }
                }
            }

            if (ok) {
                const auto committed_state = workspace_manager.build_state_dump(incoming.workspace_id);
                ManagedWorkspaceLayoutSnapshot snapshot {
                    .workspace_id = incoming.workspace_id,
                    .rectangles_by_client_id = {},
                    .visible_client_ids = *visible_clients_opt,
                    .hidden_client_ids = *hidden_clients_opt,
                    .stacking_order = *stacking_order_opt,
                    .selected_client = committed_state.selected_client,
                    .input_mode = committed_state.input_mode,
                    .managing_emacs_client_id = workspace_manager.emacs_client(incoming.workspace_id),
                };
                for (const auto& rectangle : *visible_rectangles) {
                    snapshot.rectangles_by_client_id.emplace(rectangle.client_id, ClientRect {
                        .x = rectangle.x,
                        .y = rectangle.y,
                        .width = rectangle.width,
                        .height = rectangle.height,
                    });
                }

                ok = workspace_manager.apply_managed_layout_snapshot(std::move(snapshot));
                if (!ok) {
                    error = "set-layout snapshot commit rejected";
                } else {
                    for (const auto& hidden_client : *hidden_clients_opt) {
                        if (!layout_applier.hide_client(hidden_client, incoming.workspace_id)) {
                            std::cerr << "[hyprmacs] set-layout hide failed workspace=" << incoming.workspace_id
                                      << " client=" << hidden_client << '\n';
                        }
                    }
                    for (const auto& visible_client : *visible_clients_opt) {
                        const bool was_hidden = layout_applier.is_hidden(visible_client);
                        if (!layout_applier.show_client(visible_client)) {
                            std::cerr << "[hyprmacs] set-layout show failed workspace=" << incoming.workspace_id
                                      << " client=" << visible_client << '\n';
                        } else if (was_hidden) {
                            workspace_manager.note_internal_focus_request(incoming.workspace_id, visible_client);
                        }
                    }

                    if (recalc_requester) {
                        const bool recalc_requested = recalc_requester(incoming.workspace_id);
                        if (!recalc_requested) {
                            std::cerr << "[hyprmacs] set-layout recalc request failed workspace=" << incoming.workspace_id << '\n';
                        }
                    }

                    if (focus_controller != nullptr) {
                        const auto emacs_client = workspace_manager.emacs_client(incoming.workspace_id);
                        if (emacs_client.has_value()) {
                            if (!focus_controller->alter_zorder(*emacs_client, false)) {
                                std::cerr << "[hyprmacs] set-layout z-order lower failed workspace=" << incoming.workspace_id
                                          << " client=" << *emacs_client << '\n';
                            }
                        }
                    }

                    if (selected_client.has_value() && !selected_client->empty()) {
                        const bool selected_ok = workspace_manager.set_selected_client(incoming.workspace_id, *selected_client);
                        if (!selected_ok) {
                            std::cerr << "[hyprmacs] set-layout ignored non-managed selected_client workspace="
                                      << incoming.workspace_id << " client=" << *selected_client << '\n';
                        }
                    }

                    if (input_mode.has_value()) {
                        ok = workspace_manager.set_input_mode(incoming.workspace_id, *input_mode);
                        if (!ok) {
                            error = "set-layout input_mode rejected";
                        } else if (focus_controller != nullptr && *input_mode == InputMode::kEmacsControl) {
                            const auto emacs_client = workspace_manager.emacs_client(incoming.workspace_id);
                            if (emacs_client.has_value()) {
                                (void)focus_controller->focus_client(*emacs_client);
                                if (!focus_controller->alter_zorder(*emacs_client, false)) {
                                    std::cerr << "[hyprmacs] set-layout emacs-control z-order lower failed workspace="
                                              << incoming.workspace_id << " client=" << *emacs_client << '\n';
                                }
                            }
                        }
                    }

                    apply_managed_overlay_commands(
                        layout_applier,
                        &workspace_manager,
                        incoming.workspace_id,
                        *stacking_order_opt,
                        *visible_rectangles
                    );
                }
            }
        }

        if (!ok && !error.empty()) {
            std::cerr << "[hyprmacs] set-layout failed workspace=" << incoming.workspace_id << " reason=" << error << '\n';
        }

        const auto state = workspace_manager.build_state_dump(incoming.workspace_id);
        const std::string selected_wire = state.selected_client.value_or("");
        out.push_back(make_message("layout-applied", incoming.workspace_id, payload_for_layout_applied(selected_wire, ok)));
        out.push_back(make_message("state-dump", incoming.workspace_id, serialize_state_dump_payload(state)));
        return out;
    }

    if (incoming.type == "seed-client") {
        const auto client_id = parse_string_field_from_payload(incoming.payload_json, "client_id");
        const auto workspace_id = parse_string_field_from_payload(incoming.payload_json, "workspace_id");
        const auto app_id = parse_string_field_from_payload(incoming.payload_json, "app_id");
        const auto title = parse_string_field_from_payload(incoming.payload_json, "title");
        const auto floating = parse_bool_field_from_payload(incoming.payload_json, "floating");
        if (client_id.has_value() && workspace_id.has_value() && app_id.has_value() && title.has_value() && floating.has_value()) {
            workspace_manager.seed_client(*client_id, *workspace_id, *app_id, *title, *floating);
            out.push_back(make_message(
                "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
            ));
        }
        return out;
    }

    out.push_back(make_message(
        "protocol-error",
        incoming.workspace_id,
        payload_for_protocol_error("unknown-type", "unsupported message type", incoming.type)
    ));
    return out;
}

ProtocolMessage focus_request_message(const WorkspaceId& workspace_id, const ClientId& client_id) {
    return make_message("client-focus-requested", workspace_id, payload_for_focus_request(client_id));
}

bool controller_send_target_is_current(int candidate_fd, std::uint64_t candidate_generation, int current_fd,
                                       std::uint64_t current_generation) {
    return candidate_fd >= 0 && candidate_fd == current_fd && candidate_generation == current_generation;
}

SendProtocolResult send_protocol_message(int fd, const ProtocolMessage& message, int flags) {
    if (fd < 0) {
        return SendProtocolResult::Failed;
    }

    const std::string encoded = serialize_message(message) + "\n";
    int send_flags = flags;
#ifdef MSG_NOSIGNAL
    send_flags |= MSG_NOSIGNAL;
#endif
    size_t offset = 0;
    while (offset < encoded.size()) {
        const ssize_t sent = ::send(fd, encoded.data() + offset, encoded.size() - offset, send_flags);
        if (sent > 0) {
            offset += static_cast<size_t>(sent);
            continue;
        }
        if (sent == 0) {
            return offset == 0 ? SendProtocolResult::Failed : SendProtocolResult::Partial;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return offset == 0 ? SendProtocolResult::WouldBlock : SendProtocolResult::Partial;
        }
        return offset == 0 ? SendProtocolResult::Failed : SendProtocolResult::Partial;
    }

    return SendProtocolResult::Sent;
}

ControllerSendAction controller_send_action_for_result(SendProtocolResult result) {
    switch (result) {
        case SendProtocolResult::Sent:
            return ControllerSendAction::KeepConnected;
        case SendProtocolResult::WouldBlock:
            return ControllerSendAction::DropFrame;
        case SendProtocolResult::Partial:
        case SendProtocolResult::Failed:
            return ControllerSendAction::Disconnect;
    }
    return ControllerSendAction::Disconnect;
}

IpcServer::IpcServer(WorkspaceManager* workspace_manager, LayoutApplier* layout_applier, FocusController* focus_controller,
                     RecalcRequester recalc_requester)
    : workspace_manager_(workspace_manager)
    , layout_applier_(layout_applier)
    , focus_controller_(focus_controller)
    , recalc_requester_(std::move(recalc_requester)) {
    if (workspace_manager_ != nullptr) {
        workspace_manager_->set_client_transition_notifier(
            [this](const WorkspaceId& workspace_id, const ClientId& client_id, bool floating) {
                on_client_transition(workspace_id, client_id, floating);
            }
        );
        workspace_manager_->set_state_change_notifier(
            [this](const WorkspaceId& workspace_id) {
                on_workspace_state_changed(workspace_id);
            }
        );
        workspace_manager_->set_focus_request_notifier(
            [this](const WorkspaceId& workspace_id, const ClientId& client_id) {
                on_focus_request(workspace_id, client_id);
            }
        );
    }
}

IpcServer::~IpcServer() {
    if (workspace_manager_ != nullptr) {
        workspace_manager_->set_client_transition_notifier({});
        workspace_manager_->set_state_change_notifier({});
        workspace_manager_->set_focus_request_notifier({});
    }
    stop();
}

bool IpcServer::start() {
    if (workspace_manager_ == nullptr || layout_applier_ == nullptr) {
        std::cerr << "[hyprmacs] ipc server start failed: missing dependencies\n";
        return false;
    }

    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return true;
    }

    socket_path_ = default_ipc_socket_path();
    if (!socket_path_.has_value()) {
        std::cerr << "[hyprmacs] ipc server disabled: missing XDG_RUNTIME_DIR\n";
        running_.store(false);
        return false;
    }

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[hyprmacs] ipc server socket() failed: " << std::strerror(errno) << '\n';
        running_.store(false);
        return false;
    }

    ::unlink(socket_path_->c_str());

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (socket_path_->size() >= sizeof(addr.sun_path)) {
        std::cerr << "[hyprmacs] ipc server path too long: " << *socket_path_ << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }
    std::memcpy(addr.sun_path, socket_path_->c_str(), socket_path_->size() + 1);

    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[hyprmacs] ipc server bind failed: " << std::strerror(errno) << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }

    if (::listen(listen_fd_, 4) != 0) {
        std::cerr << "[hyprmacs] ipc server listen failed: " << std::strerror(errno) << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }

    std::cerr << "[hyprmacs] ipc server listening at " << *socket_path_ << '\n';

    // Resolve plugin setting after startup to avoid querying Hyprland command socket
    // from PLUGIN_INIT on the compositor thread.
    state_notify_debounce_ms_ = kDefaultStateNotifyDebounceMs;
    if (state_notify_debounce_ms_ > 0) {
        state_notify_thread_ = std::thread([this]() {
            state_notify_loop();
        });
    }

    accept_thread_ = std::thread([this]() {
        accept_loop();
    });
    return true;
}

void IpcServer::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    {
        std::unique_lock controller_lock(controller_mutex_);
        std::unique_lock send_lock(send_mutex_);
        if (controller_fd_ >= 0) {
            ::shutdown(controller_fd_, SHUT_RDWR);
            ::close(controller_fd_);
            controller_fd_ = -1;
            controller_generation_ += 1;
        }
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    state_notify_cv_.notify_all();
    if (state_notify_thread_.joinable()) {
        state_notify_thread_.join();
    }
    {
        std::scoped_lock lock(state_notify_mutex_);
        pending_state_notify_deadlines_.clear();
    }

    if (socket_path_.has_value()) {
        ::unlink(socket_path_->c_str());
    }

    if (workspace_manager_ != nullptr) {
        restore_workspace_on_disconnect();
        workspace_manager_->set_controller_connected(false);
    }
}

std::optional<std::string> IpcServer::socket_path() const {
    return socket_path_;
}

void IpcServer::publish_state_dump_for_workspace(const WorkspaceId& workspace_id) {
    if (!running_.load()) {
        return;
    }

    // Dispatcher callbacks run on Hyprland's command handling path.
    // Keep this path state-dump only: avoid overlay dispatches that would
    // synchronously re-enter Hyprland's command socket and can stall hyprctl.
    if (state_notify_debounce_ms_ <= 0) {
        publish_state_dump_now(workspace_id);
    } else {
        enqueue_debounced_state_dump(workspace_id);
    }
}

int IpcServer::resolve_state_notify_debounce_ms() const {
    std::optional<int> configured_value;
    if (workspace_manager_ != nullptr) {
        configured_value = workspace_manager_->plugin_option_int("plugin:hyprmacs:state_notify_debounce_ms");
    }

    bool used_default = false;
    bool clamped = false;
    const int debounce_ms = normalize_state_notify_debounce_ms(configured_value, &used_default, &clamped);
    if (used_default) {
        std::cerr << "[hyprmacs] state notify debounce setting missing; using default "
                  << kDefaultStateNotifyDebounceMs << "ms\n";
    } else if (clamped && configured_value.has_value()) {
        std::cerr << "[hyprmacs] state notify debounce out of range (" << *configured_value
                  << "); clamped to " << debounce_ms << "ms\n";
    }
    return debounce_ms;
}

void IpcServer::enqueue_debounced_state_dump(const WorkspaceId& workspace_id) {
    if (!running_.load()) {
        return;
    }
    if (state_notify_debounce_ms_ <= 0) {
        publish_state_dump_now(workspace_id);
        return;
    }

    std::uint64_t controller_generation = 0;
    {
        std::scoped_lock lock(controller_mutex_);
        if (controller_fd_ < 0) {
            return;
        }
        controller_generation = controller_generation_;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(state_notify_debounce_ms_);
    bool inserted = false;
    {
        std::scoped_lock lock(state_notify_mutex_);
        auto it = pending_state_notify_deadlines_.find(workspace_id);
        if (it == pending_state_notify_deadlines_.end()) {
            pending_state_notify_deadlines_.emplace(
                workspace_id,
                PendingStateNotification {
                    .deadline = deadline,
                    .controller_generation = controller_generation,
                }
            );
            inserted = true;
        } else if (it->second.controller_generation != controller_generation) {
            it->second = PendingStateNotification {
                .deadline = deadline,
                .controller_generation = controller_generation,
            };
            inserted = true;
        }
    }
    if (inserted) {
        state_notify_cv_.notify_one();
    }
}

void IpcServer::publish_state_dump_now(const WorkspaceId& workspace_id, std::optional<std::uint64_t> expected_generation) {
    if (!running_.load() || workspace_manager_ == nullptr) {
        return;
    }

    send_controller_message(
        make_message("state-dump", workspace_id, serialize_state_dump_payload(workspace_manager_->build_state_dump(workspace_id))),
        expected_generation
    );
}

void IpcServer::state_notify_loop() {
    while (running_.load()) {
        std::vector<std::pair<WorkspaceId, std::uint64_t>> due_workspaces;
        {
            std::unique_lock lock(state_notify_mutex_);
            state_notify_cv_.wait(lock, [this]() {
                return !running_.load() || !pending_state_notify_deadlines_.empty();
            });
            if (!running_.load()) {
                break;
            }

            auto next_deadline_it = std::min_element(
                pending_state_notify_deadlines_.begin(),
                pending_state_notify_deadlines_.end(),
                [](const auto& lhs, const auto& rhs) { return lhs.second.deadline < rhs.second.deadline; }
            );
            if (next_deadline_it == pending_state_notify_deadlines_.end()) {
                continue;
            }

            const auto next_deadline = next_deadline_it->second.deadline;
            state_notify_cv_.wait_until(lock, next_deadline, [this, next_deadline]() {
                return !running_.load() || pending_state_notify_deadlines_.empty() ||
                       std::chrono::steady_clock::now() >= next_deadline;
            });
            if (!running_.load()) {
                break;
            }

            const auto now = std::chrono::steady_clock::now();
            for (auto it = pending_state_notify_deadlines_.begin(); it != pending_state_notify_deadlines_.end();) {
                if (it->second.deadline <= now) {
                    due_workspaces.emplace_back(it->first, it->second.controller_generation);
                    it = pending_state_notify_deadlines_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        for (const auto& [workspace_id, controller_generation] : due_workspaces) {
            publish_state_dump_now(workspace_id, controller_generation);
        }
    }
}

void IpcServer::accept_loop() {
    while (running_.load()) {
        int controller_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (controller_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (running_.load()) {
                std::cerr << "[hyprmacs] ipc server accept failed: " << std::strerror(errno) << '\n';
            }
            break;
        }

        std::uint64_t controller_generation = 0;
        {
            std::scoped_lock lock(controller_mutex_);
            controller_fd_ = controller_fd;
            controller_generation_ += 1;
            controller_generation = controller_generation_;
        }
        {
            std::scoped_lock lock(state_notify_mutex_);
            pending_state_notify_deadlines_.clear();
        }
        state_notify_cv_.notify_all();
        state_notify_debounce_ms_ = resolve_state_notify_debounce_ms();
        workspace_manager_->set_controller_connected(true);
        std::cerr << "[hyprmacs] ipc controller connected\n";
        serve_controller(controller_fd, controller_generation);
        {
            std::unique_lock controller_lock(controller_mutex_);
            std::unique_lock send_lock(send_mutex_);
            if (controller_fd_ == controller_fd) {
                ::shutdown(controller_fd_, SHUT_RDWR);
                ::close(controller_fd_);
                controller_fd_ = -1;
                controller_generation_ += 1;
            }
        }
        {
            std::scoped_lock lock(state_notify_mutex_);
            pending_state_notify_deadlines_.clear();
        }
        state_notify_cv_.notify_all();
        restore_workspace_on_disconnect();
        workspace_manager_->set_controller_connected(false);
        std::cerr << "[hyprmacs] ipc controller disconnected\n";
    }
}

void IpcServer::serve_controller(int controller_fd, std::uint64_t controller_generation) {
    std::array<char, 4096> chunk {};
    std::string pending;

    while (running_.load()) {
        const ssize_t read_bytes = ::recv(controller_fd, chunk.data(), chunk.size(), 0);
        if (read_bytes <= 0) {
            if (read_bytes < 0 && errno == EINTR) {
                continue;
            }
            break;
        }

        pending.append(chunk.data(), static_cast<size_t>(read_bytes));

        size_t newline = pending.find('\n');
        while (newline != std::string::npos) {
            const std::string frame = pending.substr(0, newline);
            pending.erase(0, newline + 1);

            if (!frame.empty()) {
                const auto incoming = parse_message(frame);
                if (!incoming.has_value()) {
                    std::cerr << "[hyprmacs] ipc invalid frame dropped\n";
                    send_controller_message(
                        make_message(
                            "protocol-error",
                            "",
                            payload_for_protocol_error("invalid-frame", "malformed JSON envelope")
                        ),
                        controller_generation
                    );
                } else if (incoming->version != 1) {
                    std::cerr << "[hyprmacs] ipc unsupported version: " << incoming->version << '\n';
                    send_controller_message(
                        make_message(
                            "protocol-error",
                            incoming->workspace_id,
                            payload_for_protocol_error(
                                "unsupported-version", "unsupported protocol version", std::to_string(incoming->version)
                            )
                        ),
                        controller_generation
                    );
                } else {
                    std::cerr << "[hyprmacs] ipc recv type=" << incoming->type << " workspace=" << incoming->workspace_id << '\n';
                    const auto responses = route_command_for_tests(
                        *incoming,
                        *workspace_manager_,
                        *layout_applier_,
                        focus_controller_,
                        recalc_requester_
                    );
                    for (const auto& response : responses) {
                        send_controller_message(response, controller_generation);
                    }
                }
            }

            newline = pending.find('\n');
        }
    }
}

void IpcServer::restore_workspace_on_disconnect() {
    if (workspace_manager_ == nullptr || layout_applier_ == nullptr) {
        return;
    }

    const auto workspace_id = workspace_manager_->managed_workspace();
    if (!workspace_id.has_value()) {
        return;
    }

    const auto state = workspace_manager_->build_state_dump(*workspace_id);
    const bool ok = layout_applier_->restore_workspace_to_native(*workspace_id, state.managed_clients);
    if (!ok) {
        std::cerr << "[hyprmacs] disconnect cleanup had partial restore failures workspace=" << *workspace_id << '\n';
    }
}

void IpcServer::on_client_transition(const WorkspaceId& workspace_id, const ClientId& client_id, bool floating) {
    if (layout_applier_ != nullptr) {
        if (floating) {
            (void)layout_applier_->show_client(client_id);
        } else {
            (void)layout_applier_->hide_client(client_id, workspace_id);
        }
    }

    if (!running_.load() || workspace_manager_ == nullptr) {
        return;
    }

    const char* type = floating ? "client-became-floating" : "client-became-tiled";
    send_controller_message(make_message(type, workspace_id, payload_for_client_transition(client_id, floating)));
    on_workspace_state_changed(workspace_id);
}

void IpcServer::on_workspace_state_changed(const WorkspaceId& workspace_id) {
    if (!running_.load()) {
        return;
    }
    if (workspace_manager_ != nullptr) {
        (void)workspace_manager_->refresh_workspace_floating_state_from_query(workspace_id, false);
    }
    if (workspace_manager_ != nullptr && layout_applier_ != nullptr) {
        const auto snapshot = workspace_manager_->managed_layout_snapshot(workspace_id);
        if (snapshot.has_value() && snapshot->input_mode.has_value() &&
            *snapshot->input_mode == InputMode::kEmacsControl) {
            for (const auto& hidden_client_id : snapshot->hidden_client_ids) {
                if (!layout_applier_->hide_client_force(hidden_client_id, workspace_id)) {
                    std::cerr << "[hyprmacs] state-change hidden enforcement failed workspace="
                              << workspace_id << " client=" << hidden_client_id << '\n';
                }
            }

            std::unordered_set<std::string> visible_client_ids;
            visible_client_ids.reserve(snapshot->visible_client_ids.size());
            for (const auto& client_id : snapshot->visible_client_ids) {
                visible_client_ids.insert(client_id);
            }
            apply_managed_overlay_zorder_only(
                *layout_applier_,
                workspace_id,
                snapshot->stacking_order,
                visible_client_ids
            );

            if (focus_controller_ != nullptr && snapshot->managing_emacs_client_id.has_value()) {
                if (!focus_controller_->alter_zorder(*snapshot->managing_emacs_client_id, false)) {
                    std::cerr << "[hyprmacs] state-change emacs z-order lower failed workspace="
                              << workspace_id << " client=" << *snapshot->managing_emacs_client_id << '\n';
                }
            }
        }
    }
    if (state_notify_debounce_ms_ <= 0) {
        publish_state_dump_now(workspace_id);
    } else {
        enqueue_debounced_state_dump(workspace_id);
    }
}

void IpcServer::on_focus_request(const WorkspaceId& workspace_id, const ClientId& client_id) {
    if (!running_.load()) {
        return;
    }

    send_controller_message(focus_request_message(workspace_id, client_id));
}

bool IpcServer::send_controller_message(const ProtocolMessage& message, std::optional<std::uint64_t> expected_generation) {
    std::unique_lock controller_lock(controller_mutex_);
    const int fd = controller_fd_;
    const std::uint64_t controller_generation = controller_generation_;
    std::unique_lock send_lock(send_mutex_);
    if (!running_.load()
        || !controller_send_target_is_current(fd, controller_generation, controller_fd_, controller_generation_)
        || (expected_generation.has_value() && *expected_generation != controller_generation)) {
        return false;
    }

    const auto send_result = send_protocol_message(fd, message, MSG_DONTWAIT);
    const auto action = controller_send_action_for_result(send_result);
    if (action == ControllerSendAction::KeepConnected) {
        std::cerr << "[hyprmacs] ipc send type=" << message.type << " workspace=" << message.workspace_id << '\n';
        return true;
    }

    std::cerr << "[hyprmacs] ipc nonblocking send failed for type=" << message.type
              << " workspace=" << message.workspace_id << ": " << std::strerror(errno) << '\n';
    if (action == ControllerSendAction::Disconnect) {
        invalidate_controller_locked(fd, controller_generation);
    }
    return false;
}

void IpcServer::invalidate_controller_locked(int fd, std::uint64_t generation) {
    if (controller_send_target_is_current(fd, generation, controller_fd_, controller_generation_)) {
        ::shutdown(controller_fd_, SHUT_RDWR);
        ::close(controller_fd_);
        controller_fd_ = -1;
        controller_generation_ += 1;
    }
}

}  // namespace hyprmacs
