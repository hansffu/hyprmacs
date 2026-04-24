#include "hyprmacs/protocol.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hyprmacs {
namespace {

void skip_ws(std::string_view text, size_t* pos) {
    while (*pos < text.size() && std::isspace(static_cast<unsigned char>(text[*pos])) != 0) {
        ++(*pos);
    }
}

std::optional<std::string> parse_json_string(std::string_view text, size_t* pos) {
    if (*pos >= text.size() || text[*pos] != '"') {
        return std::nullopt;
    }
    ++(*pos);
    std::string out;
    while (*pos < text.size()) {
        const char c = text[*pos];
        ++(*pos);
        if (c == '"') {
            return out;
        }
        if (c == '\\') {
            if (*pos >= text.size()) {
                return std::nullopt;
            }
            const char e = text[*pos];
            ++(*pos);
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
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
                default:
                    return std::nullopt;
            }
            continue;
        }
        out.push_back(c);
    }
    return std::nullopt;
}

std::optional<std::string> capture_json_value(std::string_view text, size_t* pos) {
    skip_ws(text, pos);
    if (*pos >= text.size()) {
        return std::nullopt;
    }

    const size_t start = *pos;
    if (text[*pos] == '"') {
        if (!parse_json_string(text, pos)) {
            return std::nullopt;
        }
        return std::string(text.substr(start, *pos - start));
    }

    if (text[*pos] == '{' || text[*pos] == '[') {
        const char open = text[*pos];
        const char close = open == '{' ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        bool escaped = false;
        while (*pos < text.size()) {
            const char c = text[*pos];
            ++(*pos);
            if (in_string) {
                if (escaped) {
                    escaped = false;
                    continue;
                }
                if (c == '\\') {
                    escaped = true;
                    continue;
                }
                if (c == '"') {
                    in_string = false;
                }
                continue;
            }
            if (c == '"') {
                in_string = true;
                continue;
            }
            if (c == open) {
                ++depth;
            } else if (c == close) {
                --depth;
                if (depth == 0) {
                    return std::string(text.substr(start, *pos - start));
                }
            }
        }
        return std::nullopt;
    }

    while (*pos < text.size()) {
        const char c = text[*pos];
        if (c == ',' || c == '}' || c == ']') {
            break;
        }
        ++(*pos);
    }
    if (*pos == start) {
        return std::nullopt;
    }
    return std::string(text.substr(start, *pos - start));
}

std::optional<std::unordered_map<std::string, std::string>> parse_object_map(std::string_view text) {
    size_t pos = 0;
    skip_ws(text, &pos);
    if (pos >= text.size() || text[pos] != '{') {
        return std::nullopt;
    }
    ++pos;

    std::unordered_map<std::string, std::string> out;
    while (true) {
        skip_ws(text, &pos);
        if (pos >= text.size()) {
            return std::nullopt;
        }
        if (text[pos] == '}') {
            ++pos;
            break;
        }

        auto key = parse_json_string(text, &pos);
        if (!key) {
            return std::nullopt;
        }
        skip_ws(text, &pos);
        if (pos >= text.size() || text[pos] != ':') {
            return std::nullopt;
        }
        ++pos;

        auto value = capture_json_value(text, &pos);
        if (!value) {
            return std::nullopt;
        }
        out[*key] = *value;

        skip_ws(text, &pos);
        if (pos >= text.size()) {
            return std::nullopt;
        }
        if (text[pos] == ',') {
            ++pos;
            continue;
        }
        if (text[pos] == '}') {
            ++pos;
            break;
        }
        return std::nullopt;
    }
    return out;
}

std::optional<std::string> unquote(std::string_view raw) {
    size_t left = 0;
    while (left < raw.size() && std::isspace(static_cast<unsigned char>(raw[left])) != 0) {
        ++left;
    }
    size_t right = raw.size();
    while (right > left && std::isspace(static_cast<unsigned char>(raw[right - 1])) != 0) {
        --right;
    }
    raw = raw.substr(left, right - left);

    size_t pos = 0;
    auto parsed = parse_json_string(raw, &pos);
    if (!parsed) {
        return std::nullopt;
    }
    skip_ws(raw, &pos);
    if (pos != raw.size()) {
        return std::nullopt;
    }
    return parsed;
}

std::optional<int> parse_int(std::string_view raw) {
    size_t left = 0;
    while (left < raw.size() && std::isspace(static_cast<unsigned char>(raw[left])) != 0) {
        ++left;
    }
    size_t right = raw.size();
    while (right > left && std::isspace(static_cast<unsigned char>(raw[right - 1])) != 0) {
        --right;
    }
    raw = raw.substr(left, right - left);

    try {
        size_t idx = 0;
        const int value = std::stoi(std::string(raw), &idx, 10);
        if (idx != raw.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<bool> parse_bool(std::string_view raw) {
    size_t left = 0;
    while (left < raw.size() && std::isspace(static_cast<unsigned char>(raw[left])) != 0) {
        ++left;
    }
    size_t right = raw.size();
    while (right > left && std::isspace(static_cast<unsigned char>(raw[right - 1])) != 0) {
        --right;
    }
    raw = raw.substr(left, right - left);

    if (raw == "true") {
        return true;
    }
    if (raw == "false") {
        return false;
    }
    return std::nullopt;
}

std::string escape_json(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        const auto uch = static_cast<unsigned char>(c);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (uch < 0x20) {
                    std::ostringstream escaped;
                    escaped << "\\u"
                            << std::hex << std::setw(4) << std::setfill('0')
                            << static_cast<int>(uch);
                    out += escaped.str();
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::optional<std::vector<std::string>> split_array_items(std::string_view raw_array) {
    size_t pos = 0;
    skip_ws(raw_array, &pos);
    if (pos >= raw_array.size() || raw_array[pos] != '[') {
        return std::nullopt;
    }
    ++pos;

    std::vector<std::string> out;
    while (true) {
        skip_ws(raw_array, &pos);
        if (pos >= raw_array.size()) {
            return std::nullopt;
        }
        if (raw_array[pos] == ']') {
            ++pos;
            break;
        }
        auto item = capture_json_value(raw_array, &pos);
        if (!item) {
            return std::nullopt;
        }
        out.push_back(*item);

        skip_ws(raw_array, &pos);
        if (pos >= raw_array.size()) {
            return std::nullopt;
        }
        if (raw_array[pos] == ',') {
            ++pos;
            continue;
        }
        if (raw_array[pos] == ']') {
            ++pos;
            break;
        }
        return std::nullopt;
    }
    skip_ws(raw_array, &pos);
    if (pos != raw_array.size()) {
        return std::nullopt;
    }
    return out;
}

std::optional<InputMode> parse_input_mode(std::string_view raw) {
    if (raw == "null") {
        return std::nullopt;
    }
    auto mode = unquote(raw);
    if (!mode) {
        return std::nullopt;
    }
    if (*mode == "emacs-control") {
        return InputMode::kEmacsControl;
    }
    if (*mode == "client-control") {
        return InputMode::kClientControl;
    }
    return InputMode::kUnknown;
}

std::string serialize_input_mode(const std::optional<InputMode>& input_mode) {
    if (!input_mode.has_value()) {
        return "null";
    }
    if (*input_mode == InputMode::kEmacsControl) {
        return "\"emacs-control\"";
    }
    if (*input_mode == InputMode::kClientControl) {
        return "\"client-control\"";
    }
    return "\"unknown\"";
}

std::string serialize_client_snapshot(const ClientSnapshot& client) {
    std::ostringstream out;
    out << "{"
        << "\"client_id\":\"" << escape_json(client.client_id) << "\","
        << "\"title\":\"" << escape_json(client.title) << "\","
        << "\"app_id\":\"" << escape_json(client.app_id) << "\","
        << "\"floating\":" << (client.floating ? "true" : "false")
        << "}";
    return out.str();
}

}  // namespace

std::optional<ProtocolMessage> parse_message(std::string_view json) {
    const auto root = parse_object_map(json);
    if (!root) {
        return std::nullopt;
    }

    const auto version_it = root->find("version");
    const auto type_it = root->find("type");
    const auto workspace_it = root->find("workspace_id");
    const auto timestamp_it = root->find("timestamp");
    const auto payload_it = root->find("payload");
    if (version_it == root->end() || type_it == root->end() || workspace_it == root->end() ||
        timestamp_it == root->end() || payload_it == root->end()) {
        return std::nullopt;
    }

    const auto version = parse_int(version_it->second);
    const auto type = unquote(type_it->second);
    const auto workspace_id = unquote(workspace_it->second);
    const auto timestamp = unquote(timestamp_it->second);
    if (!version || !type || !workspace_id || !timestamp) {
        return std::nullopt;
    }

    return ProtocolMessage {
        .version = *version,
        .type = *type,
        .workspace_id = *workspace_id,
        .timestamp = *timestamp,
        .payload_json = payload_it->second,
    };
}

std::string serialize_message(const ProtocolMessage& message) {
    std::ostringstream out;
    out << "{"
        << "\"version\":" << message.version << ","
        << "\"type\":\"" << escape_json(message.type) << "\","
        << "\"workspace_id\":\"" << escape_json(message.workspace_id) << "\","
        << "\"timestamp\":\"" << escape_json(message.timestamp) << "\","
        << "\"payload\":" << message.payload_json
        << "}";
    return out.str();
}

std::optional<StateDumpPayload> parse_state_dump_payload(std::string_view payload_json) {
    const auto payload = parse_object_map(payload_json);
    if (!payload) {
        return std::nullopt;
    }

    const auto managed_it = payload->find("managed");
    const auto connected_it = payload->find("controller_connected");
    const auto eligible_it = payload->find("eligible_clients");
    const auto managed_clients_it = payload->find("managed_clients");
    const auto selected_it = payload->find("selected_client");
    const auto input_mode_it = payload->find("input_mode");
    if (managed_it == payload->end() || connected_it == payload->end() || eligible_it == payload->end() ||
        managed_clients_it == payload->end() || selected_it == payload->end() ||
        input_mode_it == payload->end()) {
        return std::nullopt;
    }

    const auto managed = parse_bool(managed_it->second);
    const auto controller_connected = parse_bool(connected_it->second);
    if (!managed || !controller_connected) {
        return std::nullopt;
    }

    StateDumpPayload out;
    out.managed = *managed;
    out.controller_connected = *controller_connected;

    const auto eligible_items = split_array_items(eligible_it->second);
    if (!eligible_items) {
        return std::nullopt;
    }
    for (const auto& item : *eligible_items) {
        const auto client = parse_object_map(item);
        if (!client) {
            return std::nullopt;
        }
        const auto id_it = client->find("client_id");
        const auto title_it = client->find("title");
        const auto app_id_it = client->find("app_id");
        const auto floating_it = client->find("floating");
        if (id_it == client->end() || title_it == client->end() || app_id_it == client->end() ||
            floating_it == client->end()) {
            return std::nullopt;
        }
        const auto client_id = unquote(id_it->second);
        const auto title = unquote(title_it->second);
        const auto app_id = unquote(app_id_it->second);
        const auto floating = parse_bool(floating_it->second);
        if (!client_id || !title || !app_id || !floating) {
            return std::nullopt;
        }
        out.eligible_clients.push_back(ClientSnapshot {
            .client_id = *client_id,
            .title = *title,
            .app_id = *app_id,
            .floating = *floating,
        });
    }

    const auto managed_client_items = split_array_items(managed_clients_it->second);
    if (!managed_client_items) {
        return std::nullopt;
    }
    for (const auto& item : *managed_client_items) {
        const auto managed_client = unquote(item);
        if (!managed_client) {
            return std::nullopt;
        }
        out.managed_clients.push_back(*managed_client);
    }

    if (selected_it->second == "null") {
        out.selected_client = std::nullopt;
    } else {
        const auto selected = unquote(selected_it->second);
        if (!selected) {
            return std::nullopt;
        }
        out.selected_client = *selected;
    }

    out.input_mode = parse_input_mode(input_mode_it->second);
    return out;
}

std::string serialize_state_dump_payload(const StateDumpPayload& payload) {
    std::ostringstream out;
    out << "{"
        << "\"managed\":" << (payload.managed ? "true" : "false") << ","
        << "\"controller_connected\":" << (payload.controller_connected ? "true" : "false") << ",";

    out << "\"eligible_clients\":[";
    for (size_t i = 0; i < payload.eligible_clients.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << serialize_client_snapshot(payload.eligible_clients[i]);
    }
    out << "],";

    out << "\"managed_clients\":[";
    for (size_t i = 0; i < payload.managed_clients.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << "\"" << escape_json(payload.managed_clients[i]) << "\"";
    }
    out << "],";

    if (payload.selected_client.has_value()) {
        out << "\"selected_client\":\"" << escape_json(*payload.selected_client) << "\",";
    } else {
        out << "\"selected_client\":null,";
    }

    out << "\"input_mode\":" << serialize_input_mode(payload.input_mode) << "}";
    return out.str();
}

}  // namespace hyprmacs
