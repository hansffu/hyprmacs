#include "hyprmacs/workspace_manager.hpp"

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <tuple>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace hyprmacs {
namespace {

std::optional<std::string> hyprland_event_socket_path() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    const char* instance = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (runtime_dir == nullptr || instance == nullptr || *runtime_dir == '\0' || *instance == '\0') {
        return std::nullopt;
    }

    std::string path = runtime_dir;
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    path += "hypr/";
    path += instance;
    path += "/.socket2.sock";
    return path;
}

bool connect_unix_socket(const std::string& path, int* socket_fd) {
    *socket_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (*socket_fd < 0) {
        std::cerr << "[hyprmacs] event tap socket() failed: " << std::strerror(errno) << '\n';
        return false;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;

    if (path.size() >= sizeof(addr.sun_path)) {
        std::cerr << "[hyprmacs] event tap socket path too long: " << path << '\n';
        ::close(*socket_fd);
        *socket_fd = -1;
        return false;
    }

    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    if (::connect(*socket_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[hyprmacs] event tap connect(" << path << ") failed: " << std::strerror(errno) << '\n';
        ::close(*socket_fd);
        *socket_fd = -1;
        return false;
    }

    return true;
}

std::vector<std::string> split_csv_limited(std::string_view value, size_t max_fields) {
    std::vector<std::string> fields;
    if (max_fields == 0) {
        return fields;
    }

    size_t start = 0;
    while (fields.size() + 1 < max_fields) {
        const size_t comma = value.find(',', start);
        if (comma == std::string_view::npos) {
            break;
        }
        fields.emplace_back(value.substr(start, comma - start));
        start = comma + 1;
    }
    fields.emplace_back(value.substr(start));
    return fields;
}

bool parse_boolish(std::string_view value, bool* out) {
    if (value == "1" || value == "true") {
        *out = true;
        return true;
    }
    if (value == "0" || value == "false") {
        *out = false;
        return true;
    }
    return false;
}

std::optional<bool> parse_floating_value(const std::vector<std::string>& parts) {
    for (size_t i = 1; i < parts.size(); ++i) {
        bool floating = false;
        if (parse_boolish(parts[i], &floating)) {
            return floating;
        }
    }
    return std::nullopt;
}

bool is_internal_hidden_workspace_target(std::string_view workspace_id) {
    return workspace_id == "-98" || workspace_id == "special:hyprmacs-hidden";
}

std::string normalize_client_id_for_query(std::string_view client_id) {
    if (client_id.rfind("address:", 0) == 0) {
        client_id = client_id.substr(8);
    }
    if (client_id.rfind("0x", 0) == 0) {
        return std::string(client_id);
    }
    return "0x" + std::string(client_id);
}

size_t skip_json_ws(std::string_view json, size_t pos) {
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
        ++pos;
    }
    return pos;
}

std::optional<std::string> parse_json_string_field(std::string_view json, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    const size_t key_pos = json.find(token);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }
    const size_t colon = json.find(':', key_pos + token.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    size_t pos = skip_json_ws(json, colon + 1);
    if (pos >= json.size() || json[pos] != '"') {
        return std::nullopt;
    }
    ++pos;
    const size_t end = json.find('"', pos);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(json.substr(pos, end - pos));
}

std::optional<bool> parse_json_bool_field(std::string_view json, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    const size_t key_pos = json.find(token);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }
    const size_t colon = json.find(':', key_pos + token.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const size_t pos = skip_json_ws(json, colon + 1);
    if (pos >= json.size()) {
        return std::nullopt;
    }
    if (json.compare(pos, 4, "true") == 0) {
        return true;
    }
    if (json.compare(pos, 5, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

std::optional<std::string_view> find_client_object_json(std::string_view clients_json, std::string_view client_id) {
    size_t search_from = 0;
    while (search_from < clients_json.size()) {
        const size_t address_key = clients_json.find("\"address\"", search_from);
        if (address_key == std::string_view::npos) {
            return std::nullopt;
        }

        const size_t object_start = clients_json.rfind('{', address_key);
        if (object_start == std::string_view::npos) {
            return std::nullopt;
        }

        int depth = 0;
        size_t object_end = std::string_view::npos;
        for (size_t i = object_start; i < clients_json.size(); ++i) {
            if (clients_json[i] == '{') {
                ++depth;
            } else if (clients_json[i] == '}') {
                --depth;
                if (depth == 0) {
                    object_end = i;
                    break;
                }
            }
        }
        if (object_end == std::string_view::npos) {
            return std::nullopt;
        }

        const auto object_json = clients_json.substr(object_start, object_end - object_start + 1);
        const auto address = parse_json_string_field(object_json, "address");
        if (address.has_value() && *address == client_id) {
            return object_json;
        }

        search_from = object_end + 1;
    }

    return std::nullopt;
}

}  // namespace

std::optional<EventFrame> parse_event_frame(const std::string& line) {
    if (line.empty()) {
        return std::nullopt;
    }

    const size_t split = line.find(">>");
    if (split == std::string::npos || split == 0) {
        return std::nullopt;
    }

    return EventFrame {
        .name = line.substr(0, split),
        .payload = line.substr(split + 2),
    };
}

bool is_tracked_event_name(std::string_view event_name) {
    if (event_name == "openwindow" || event_name == "openwindowv2" || event_name == "closewindow" ||
        event_name == "movewindow" || event_name == "movewindowv2" || event_name == "activewindow" ||
        event_name == "activewindowv2" || event_name == "windowtitle" || event_name == "windowtitlev2" ||
        event_name == "changefloatingmode" || event_name == "changefloatingmodev2") {
        return true;
    }

    return event_name.find("floating") != std::string_view::npos;
}

WorkspaceManager::WorkspaceManager() = default;
WorkspaceManager::WorkspaceManager(DispatchExecutor dispatch_executor, QueryExecutor query_executor)
    : dispatch_executor_(std::move(dispatch_executor))
    , query_executor_(std::move(query_executor)) {}

WorkspaceManager::~WorkspaceManager() {
    stop_event_tap();
}

bool WorkspaceManager::start_event_tap() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return true;
    }

    event_thread_ = std::thread([this]() {
        event_loop();
    });
    return true;
}

void WorkspaceManager::stop_event_tap() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    if (event_thread_.joinable()) {
        event_thread_.join();
    }
}

std::unordered_map<std::string, size_t> WorkspaceManager::event_counts() const {
    std::scoped_lock lock(mutex_);
    return event_counts_;
}

bool WorkspaceManager::manage_workspace(const WorkspaceId& workspace_id) {
    std::scoped_lock lock(mutex_);
    const bool changed = !managed_workspace_id_.has_value() || *managed_workspace_id_ != workspace_id;
    if (changed && policy_applied_) {
        restore_policy_locked();
    }
    managed_workspace_id_ = workspace_id;
    input_mode_ = InputMode::kEmacsControl;
    client_registry_.reconcile_management(managed_workspace_id_);
    apply_managed_policy_locked();
    return changed;
}

bool WorkspaceManager::unmanage_workspace(const WorkspaceId& workspace_id) {
    std::scoped_lock lock(mutex_);
    if (!managed_workspace_id_.has_value() || *managed_workspace_id_ != workspace_id) {
        return false;
    }
    managed_workspace_id_ = std::nullopt;
    input_mode_ = std::nullopt;
    client_registry_.reconcile_management(managed_workspace_id_);
    restore_policy_locked();
    return true;
}

bool WorkspaceManager::set_selected_client(const WorkspaceId& workspace_id, const ClientId& client_id) {
    std::scoped_lock lock(mutex_);
    const ClientRecord* record = client_registry_.find(client_id);
    if (record == nullptr || record->workspace_id != workspace_id || !record->managed) {
        return false;
    }
    client_registry_.set_focus(client_id);
    return true;
}

bool WorkspaceManager::set_input_mode(const WorkspaceId& workspace_id, InputMode mode) {
    std::scoped_lock lock(mutex_);
    if (!managed_workspace_id_.has_value() || *managed_workspace_id_ != workspace_id) {
        return false;
    }
    input_mode_ = mode;
    return true;
}

void WorkspaceManager::seed_client(
    const ClientId& client_id,
    const WorkspaceId& workspace_id,
    const std::string& app_id,
    const std::string& title,
    bool floating
) {
    std::scoped_lock lock(mutex_);
    client_registry_.upsert_open(client_id, workspace_id, app_id, title);
    client_registry_.set_floating(client_id, floating);
    client_registry_.reconcile_management(managed_workspace_id_);
}

std::optional<WorkspaceId> WorkspaceManager::managed_workspace() const {
    std::scoped_lock lock(mutex_);
    return managed_workspace_id_;
}

std::optional<ClientId> WorkspaceManager::selected_managed_client(const WorkspaceId& workspace_id) const {
    std::scoped_lock lock(mutex_);
    const auto snapshot = client_registry_.snapshot();
    if (!snapshot.selected_client.has_value()) {
        return std::nullopt;
    }
    const ClientRecord* selected = client_registry_.find(*snapshot.selected_client);
    if (selected == nullptr || selected->workspace_id != workspace_id || !selected->managed) {
        return std::nullopt;
    }
    return *snapshot.selected_client;
}

std::optional<ClientId> WorkspaceManager::emacs_client(const WorkspaceId& workspace_id) const {
    std::scoped_lock lock(mutex_);
    const auto snapshot = client_registry_.snapshot();
    for (const auto& client : snapshot.clients) {
        if (client.workspace_id != workspace_id) {
            continue;
        }
        std::string app = client.app_id;
        for (char& c : app) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (app.find("emacs") != std::string::npos) {
            return client.client_id;
        }
    }
    return std::nullopt;
}

void WorkspaceManager::set_client_transition_notifier(ClientTransitionNotifier notifier) {
    std::scoped_lock lock(mutex_);
    client_transition_notifier_ = std::move(notifier);
}

void WorkspaceManager::set_controller_connected(bool connected) {
    std::scoped_lock lock(mutex_);
    const bool transition_to_disconnected = controller_connected_ && !connected;
    controller_connected_ = connected;
    if (transition_to_disconnected && managed_workspace_id_.has_value()) {
        managed_workspace_id_ = std::nullopt;
        input_mode_ = std::nullopt;
        client_registry_.reconcile_management(managed_workspace_id_);
        restore_policy_locked();
    }
}

StateDumpPayload WorkspaceManager::build_state_dump(const WorkspaceId& workspace_id) const {
    std::scoped_lock lock(mutex_);

    StateDumpPayload out;
    out.managed = managed_workspace_id_.has_value() && *managed_workspace_id_ == workspace_id;
    out.controller_connected = controller_connected_;

    const auto snapshot = client_registry_.snapshot();
    for (const auto& client : snapshot.clients) {
        if (client.workspace_id != workspace_id) {
            continue;
        }

        if (client.eligible) {
            out.eligible_clients.push_back(ClientSnapshot {
                .client_id = client.client_id,
                .title = client.title,
                .app_id = client.app_id,
                .floating = client.floating,
            });
        }

        if (client.managed) {
            out.managed_clients.push_back(client.client_id);
        }
    }

    if (snapshot.selected_client.has_value()) {
        const ClientRecord* selected = client_registry_.find(*snapshot.selected_client);
        if (selected != nullptr && selected->workspace_id == workspace_id && selected->managed) {
            out.selected_client = *snapshot.selected_client;
        }
    }

    out.input_mode = input_mode_;
    return out;
}

void WorkspaceManager::process_event_for_tests(const std::string& line) {
    handle_line(line);
}

std::optional<int> WorkspaceManager::parse_int_field(std::string_view json, std::string_view key) {
    const std::string token = "\"" + std::string(key) + "\"";
    const size_t key_pos = json.find(token);
    if (key_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const size_t colon = json.find(':', key_pos + token.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    size_t pos = colon + 1;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])) != 0) {
        ++pos;
    }
    if (pos >= json.size()) {
        return std::nullopt;
    }

    size_t end = pos;
    if (json[end] == '-') {
        ++end;
    }
    while (end < json.size() && std::isdigit(static_cast<unsigned char>(json[end])) != 0) {
        ++end;
    }
    if (end == pos || (end == pos + 1 && json[pos] == '-')) {
        return std::nullopt;
    }

    try {
        return std::stoi(std::string(json.substr(pos, end - pos)));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> WorkspaceManager::query_option_int_locked(std::string_view option_name) const {
    if (!query_executor_) {
        return std::nullopt;
    }
    auto response = query_executor_(std::string("j/getoption ") + std::string(option_name));
    if (!response.has_value()) {
        return std::nullopt;
    }
    return parse_int_field(*response, "int");
}

std::optional<bool> WorkspaceManager::query_client_floating_locked(std::string_view client_id) const {
    if (!query_executor_) {
        return std::nullopt;
    }

    auto response = query_executor_("j/clients");
    if (!response.has_value()) {
        return std::nullopt;
    }

    const std::string normalized_client_id = normalize_client_id_for_query(client_id);
    const auto object_json = find_client_object_json(*response, normalized_client_id);
    if (!object_json.has_value()) {
        return std::nullopt;
    }

    return parse_json_bool_field(*object_json, "floating");
}

bool WorkspaceManager::dispatch_keyword_locked(std::string_view key, std::string_view value) const {
    if (!dispatch_executor_) {
        return false;
    }
    const std::string command = "keyword " + std::string(key) + " " + std::string(value);
    return dispatch_executor_(command) == 0;
}

void WorkspaceManager::capture_policy_snapshot_locked() {
    policy_snapshot_.follow_mouse = query_option_int_locked("input:follow_mouse");
    policy_snapshot_.animations_enabled = query_option_int_locked("animations:enabled");
    policy_snapshot_.focus_on_activate = query_option_int_locked("misc:focus_on_activate");
}

void WorkspaceManager::apply_managed_policy_locked() {
    if (policy_applied_) {
        return;
    }
    capture_policy_snapshot_locked();
    (void)dispatch_keyword_locked("input:follow_mouse", "0");
    (void)dispatch_keyword_locked("animations:enabled", "0");
    (void)dispatch_keyword_locked("misc:focus_on_activate", "0");
    policy_applied_ = true;
}

void WorkspaceManager::restore_policy_locked() {
    if (!policy_applied_) {
        return;
    }
    if (policy_snapshot_.follow_mouse.has_value()) {
        (void)dispatch_keyword_locked("input:follow_mouse", std::to_string(*policy_snapshot_.follow_mouse));
    }
    if (policy_snapshot_.animations_enabled.has_value()) {
        (void)dispatch_keyword_locked("animations:enabled", std::to_string(*policy_snapshot_.animations_enabled));
    }
    if (policy_snapshot_.focus_on_activate.has_value()) {
        (void)dispatch_keyword_locked("misc:focus_on_activate", std::to_string(*policy_snapshot_.focus_on_activate));
    }
    policy_snapshot_ = PolicySnapshot {};
    policy_applied_ = false;
}

void WorkspaceManager::event_loop() {
    const auto socket_path = hyprland_event_socket_path();
    if (!socket_path.has_value()) {
        std::cerr << "[hyprmacs] event tap disabled: missing runtime env vars\n";
        return;
    }

    int socket_fd = -1;
    if (!connect_unix_socket(*socket_path, &socket_fd)) {
        return;
    }

    std::cerr << "[hyprmacs] event tap connected: " << *socket_path << '\n';

    std::array<char, 4096> chunk {};
    std::string pending;

    while (running_.load()) {
        const ssize_t read_bytes = ::recv(socket_fd, chunk.data(), chunk.size(), 0);
        if (read_bytes <= 0) {
            if (read_bytes < 0 && errno == EINTR) {
                continue;
            }
            break;
        }

        pending.append(chunk.data(), static_cast<size_t>(read_bytes));

        size_t newline = pending.find('\n');
        while (newline != std::string::npos) {
            const std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            handle_line(line);
            newline = pending.find('\n');
        }
    }

    ::shutdown(socket_fd, SHUT_RDWR);
    ::close(socket_fd);
}

void WorkspaceManager::handle_line(const std::string& line) {
    const auto frame = parse_event_frame(line);
    if (!frame.has_value()) {
        return;
    }

    if (!should_track(frame->name)) {
        return;
    }

    bool state_mutated = false;
    bool transition_notify = false;
    bool transition_floating = false;
    WorkspaceId transition_workspace;
    ClientId transition_client_id;
    ClientTransitionNotifier transition_notifier;

    {
        std::scoped_lock lock(mutex_);
        event_counts_[frame->name] += 1;
        auto maybe_set_transition = [&](std::string_view raw_client_id, bool floating_value) {
            const auto after = client_registry_.find(std::string(raw_client_id));
            if (after == nullptr || !managed_workspace_id_.has_value()) {
                return;
            }
            const bool can_notify = controller_connected_ && client_transition_notifier_ != nullptr
                                    && after->workspace_id == *managed_workspace_id_;
            if (!can_notify) {
                return;
            }
            if (floating_value && !transition_notify) {
                transition_notify = true;
                transition_floating = true;
                transition_workspace = *managed_workspace_id_;
                transition_client_id = after->client_id;
                transition_notifier = client_transition_notifier_;
            } else if (!floating_value && !transition_notify) {
                transition_notify = true;
                transition_floating = false;
                transition_workspace = *managed_workspace_id_;
                transition_client_id = after->client_id;
                transition_notifier = client_transition_notifier_;
            }
        };
        auto refresh_floating_from_query = [&](std::string_view raw_client_id) {
            const auto before = client_registry_.find(std::string(raw_client_id));
            if (before == nullptr) {
                return;
            }
            const bool was_managed = before->managed;
            const auto queried_floating = query_client_floating_locked(raw_client_id);
            if (!queried_floating.has_value() || *queried_floating == before->floating) {
                return;
            }

            client_registry_.set_floating(std::string(raw_client_id), *queried_floating);
            client_registry_.reconcile_management(managed_workspace_id_);
            state_mutated = true;

            const auto after = client_registry_.find(std::string(raw_client_id));
            const bool now_managed = after != nullptr && after->managed;
            if (*queried_floating && was_managed && !now_managed) {
                maybe_set_transition(raw_client_id, true);
            } else if (!*queried_floating && !was_managed && now_managed) {
                maybe_set_transition(raw_client_id, false);
            }
        };

        if (frame->name == "openwindow" || frame->name == "openwindowv2") {
            const auto parts = split_csv_limited(frame->payload, 4);
            if (parts.size() == 4) {
                client_registry_.upsert_open(parts[0], parts[1], parts[2], parts[3]);
                client_registry_.reconcile_management(managed_workspace_id_);
                refresh_floating_from_query(parts[0]);
                state_mutated = true;
            }
        } else if (frame->name == "closewindow") {
            const auto parts = split_csv_limited(frame->payload, 2);
            if (!parts.empty()) {
                client_registry_.erase(parts[0]);
                client_registry_.reconcile_management(managed_workspace_id_);
                state_mutated = true;
            }
        } else if (frame->name == "movewindow" || frame->name == "movewindowv2") {
            const auto parts = split_csv_limited(frame->payload, 3);
            if (parts.size() >= 2) {
                if (!is_internal_hidden_workspace_target(parts[1])) {
                    client_registry_.update_workspace(parts[0], parts[1]);
                    client_registry_.reconcile_management(managed_workspace_id_);
                    state_mutated = true;
                }
            }
        } else if (frame->name == "activewindowv2") {
            const auto parts = split_csv_limited(frame->payload, 2);
            if (!parts.empty()) {
                client_registry_.set_focus(parts[0]);
                refresh_floating_from_query(parts[0]);
                state_mutated = true;
            }
        } else if (frame->name == "windowtitlev2") {
            const auto parts = split_csv_limited(frame->payload, 2);
            if (parts.size() == 2) {
                client_registry_.update_title(parts[0], parts[1]);
                client_registry_.reconcile_management(managed_workspace_id_);
                refresh_floating_from_query(parts[0]);
                state_mutated = true;
            }
        } else if (frame->name == "changefloatingmode" || frame->name == "changefloatingmodev2" ||
                   frame->name.find("floating") != std::string::npos) {
            const auto parts = split_csv_limited(frame->payload, 3);
            if (parts.size() >= 2) {
                const auto before = client_registry_.find(parts[0]);
                const bool was_managed = before != nullptr && before->managed;
                const auto floating = parse_floating_value(parts);
                if (floating.has_value()) {
                    client_registry_.set_floating(parts[0], *floating);
                    client_registry_.reconcile_management(managed_workspace_id_);
                    state_mutated = true;

                    const auto after = client_registry_.find(parts[0]);
                    const bool now_managed = after != nullptr && after->managed;
                    const bool can_notify = controller_connected_ && managed_workspace_id_.has_value() &&
                                            client_transition_notifier_ != nullptr && after != nullptr &&
                                            after->workspace_id == *managed_workspace_id_;
                    if (can_notify) {
                        if (*floating && was_managed && !now_managed) {
                            maybe_set_transition(parts[0], true);
                        } else if (!*floating && !was_managed && now_managed) {
                            maybe_set_transition(parts[0], false);
                        }
                    }
                }
            }
        }

        std::cerr << "[hyprmacs] event: " << frame->name;
        if (!frame->payload.empty()) {
            std::cerr << " payload=" << frame->payload;
        }
        std::cerr << '\n';

        if (state_mutated) {
            log_state_dump_locked();
        }
    }

    if (transition_notify && transition_notifier != nullptr) {
        transition_notifier(transition_workspace, transition_client_id, transition_floating);
    }
}

bool WorkspaceManager::should_track(const std::string& event_name) const {
    return is_tracked_event_name(event_name);
}

void WorkspaceManager::log_state_dump_locked() const {
    const auto snapshot = client_registry_.snapshot();
    size_t eligible_count = 0;
    for (const auto& client : snapshot.clients) {
        if (client.eligible) {
            ++eligible_count;
        }
    }

    std::cerr << "[hyprmacs] state-dump clients=" << snapshot.clients.size() << " eligible=" << eligible_count
              << " selected=" << (snapshot.selected_client.has_value() ? *snapshot.selected_client : "none") << '\n';

    for (const auto& client : snapshot.clients) {
        std::cerr << "[hyprmacs] state-dump client id=" << client.client_id << " ws=" << client.workspace_id
                  << " app=" << client.app_id << " floating=" << (client.floating ? "true" : "false")
                  << " eligible=" << (client.eligible ? "true" : "false") << " selected="
                  << (client.selected ? "true" : "false") << " title=" << client.title << '\n';
    }
}

}  // namespace hyprmacs
