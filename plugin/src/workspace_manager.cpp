#include "hyprmacs/workspace_manager.hpp"

#include <algorithm>
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

constexpr std::chrono::milliseconds kPendingInternalFocusRequestTtl {500};

std::chrono::steady_clock::time_point default_now() {
    return std::chrono::steady_clock::now();
}

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

std::optional<int> parse_json_int_field(std::string_view json, std::string_view key) {
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

bool client_object_in_internal_hidden_workspace(std::string_view object_json) {
    const auto workspace_id = parse_json_int_field(object_json, "id");
    if (workspace_id.has_value() && *workspace_id == -98) {
        return true;
    }

    const auto workspace_name = parse_json_string_field(object_json, "name");
    return workspace_name.has_value() && is_internal_hidden_workspace_target(*workspace_name);
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
        if (address.has_value() &&
            normalize_client_id_for_query(*address) == normalize_client_id_for_query(client_id)) {
            return object_json;
        }

        search_from = object_end + 1;
    }

    return std::nullopt;
}

std::vector<std::string_view> client_objects_json(std::string_view clients_json) {
    std::vector<std::string_view> objects;
    size_t search_from = 0;
    while (search_from < clients_json.size()) {
        const size_t address_key = clients_json.find("\"address\"", search_from);
        if (address_key == std::string_view::npos) {
            break;
        }

        const size_t object_start = clients_json.rfind('{', address_key);
        if (object_start == std::string_view::npos) {
            break;
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
            break;
        }

        objects.push_back(clients_json.substr(object_start, object_end - object_start + 1));
        search_from = object_end + 1;
    }

    return objects;
}

std::optional<std::string_view> find_workspace_object_json(std::string_view workspaces_json, std::string_view workspace_id) {
    int expected_workspace_id = 0;
    try {
        expected_workspace_id = std::stoi(std::string(workspace_id));
    } catch (...) {
        return std::nullopt;
    }

    size_t search_from = 0;
    while (search_from < workspaces_json.size()) {
        const size_t id_key = workspaces_json.find("\"id\"", search_from);
        if (id_key == std::string_view::npos) {
            return std::nullopt;
        }

        const size_t object_start = workspaces_json.rfind('{', id_key);
        if (object_start == std::string_view::npos) {
            return std::nullopt;
        }

        int depth = 0;
        size_t object_end = std::string_view::npos;
        for (size_t i = object_start; i < workspaces_json.size(); ++i) {
            if (workspaces_json[i] == '{') {
                ++depth;
            } else if (workspaces_json[i] == '}') {
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

        const auto object_json = workspaces_json.substr(object_start, object_end - object_start + 1);
        const auto id = parse_json_int_field(object_json, "id");
        if (id.has_value() && *id == expected_workspace_id) {
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

WorkspaceManager::WorkspaceManager()
    : clock_(default_now) {}

WorkspaceManager::WorkspaceManager(DispatchExecutor dispatch_executor, QueryExecutor query_executor, Clock clock)
    : dispatch_executor_(std::move(dispatch_executor))
    , query_executor_(std::move(query_executor))
    , clock_(std::move(clock)) {
    if (!clock_) {
        clock_ = default_now;
    }
}

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
    if (changed && managed_workspace_id_.has_value()) {
        restore_managed_layout_locked(*managed_workspace_id_);
        managed_layout_snapshot_.reset();
    }

    managed_workspace_id_ = workspace_id;
    input_mode_ = InputMode::kEmacsControl;
    (void)refresh_workspace_floating_state_locked(workspace_id, true, false);
    client_registry_.reconcile_management(managed_workspace_id_);
    refresh_managing_emacs_client_locked();
    sync_committed_layout_snapshot_locked();
    if (changed) {
        apply_managed_layout_locked(workspace_id);
        managed_client_seen_.clear();
        overlay_float_pending_clients_.clear();
        clear_pending_internal_focus_requests_locked();
        const auto snapshot = client_registry_.snapshot();
        for (const auto& client : snapshot.clients) {
            if (client.managed) {
                managed_client_seen_.insert(client.client_id);
            }
        }
    }

    return changed;
}

bool WorkspaceManager::unmanage_workspace(const WorkspaceId& workspace_id) {
    std::scoped_lock lock(mutex_);
    if (!managed_workspace_id_.has_value() || *managed_workspace_id_ != workspace_id) {
        return false;
    }

    restore_managed_layout_locked(workspace_id);
    managed_layout_snapshot_.reset();
    managed_workspace_id_ = std::nullopt;
    input_mode_ = std::nullopt;
    last_active_client_id_ = std::nullopt;
    managing_emacs_client_id_ = std::nullopt;
    client_registry_.reconcile_management(managed_workspace_id_);
    managed_client_seen_.clear();
    overlay_float_pending_clients_.clear();
    clear_pending_internal_focus_requests_locked();
    return true;
}

bool WorkspaceManager::can_float_managed_client(const WorkspaceId& workspace_id, const ClientId& client_id) const {
    std::scoped_lock lock(mutex_);
    if (!managed_workspace_id_.has_value() || *managed_workspace_id_ != workspace_id) {
        return false;
    }

    const std::string normalized = normalize_client_id_for_query(client_id);
    const ClientRecord* record = client_registry_.find(normalized);
    return record != nullptr && record->managed;
}

bool WorkspaceManager::float_managed_client(const WorkspaceId& workspace_id, const ClientId& client_id) {
    std::scoped_lock lock(mutex_);
    if (!managed_workspace_id_.has_value() || *managed_workspace_id_ != workspace_id) {
        return false;
    }

    const std::string normalized = normalize_client_id_for_query(client_id);
    const ClientRecord* before = client_registry_.find(normalized);
    if (before == nullptr || !before->managed) {
        return false;
    }

    if (managed_layout_snapshot_.has_value() && managed_layout_snapshot_->workspace_id == workspace_id &&
        is_snapshot_visible_client_locked(normalized)) {
        overlay_float_pending_clients_.insert(normalized);
    }
    client_registry_.set_floating(normalized, true);
    client_registry_.reconcile_management(managed_workspace_id_);
    managed_client_seen_.erase(normalized);
    sync_committed_layout_snapshot_locked();
    return true;
}

std::vector<SummonCandidate> WorkspaceManager::summon_candidates(const WorkspaceId& target_workspace_id) const {
    std::scoped_lock lock(mutex_);
    std::vector<SummonCandidate> out;
    if (!managed_workspace_id_.has_value() || *managed_workspace_id_ != target_workspace_id) {
        return out;
    }

    const auto snapshot = client_registry_.snapshot();
    for (const auto& client : snapshot.clients) {
        if (client.workspace_id == target_workspace_id || !client.eligible || client.floating) {
            continue;
        }
        out.push_back(SummonCandidate {
            .client_id = client.client_id,
            .workspace_id = client.workspace_id,
            .app_id = client.app_id,
            .title = client.title,
        });
    }

    return out;
}

bool WorkspaceManager::summon_client(const WorkspaceId& target_workspace_id, const ClientId& client_id) {
    std::scoped_lock lock(mutex_);
    if (!managed_workspace_id_.has_value() || *managed_workspace_id_ != target_workspace_id) {
        return false;
    }

    const std::string normalized = normalize_client_id_for_query(client_id);
    const ClientRecord* before = client_registry_.find(normalized);
    if (before == nullptr || before->workspace_id == target_workspace_id || !before->eligible || before->floating) {
        return false;
    }

    if (!dispatch_executor_) {
        return false;
    }

    const std::string command = "dispatch movetoworkspacesilent " + target_workspace_id + ",address:" + normalized;
    if (dispatch_executor_(command) != 0) {
        return false;
    }

    client_registry_.update_workspace(normalized, target_workspace_id);
    client_registry_.reconcile_management(managed_workspace_id_);
    managed_client_seen_.insert(normalized);
    sync_committed_layout_snapshot_locked();
    return true;
}

bool WorkspaceManager::set_selected_client(const WorkspaceId& workspace_id, const ClientId& client_id) {
    std::scoped_lock lock(mutex_);
    const ClientRecord* record = client_registry_.find(client_id);
    if (record == nullptr || record->workspace_id != workspace_id || !record->managed) {
        return false;
    }
    client_registry_.set_focus(client_id);
    sync_committed_layout_snapshot_locked();
    return true;
}

bool WorkspaceManager::set_input_mode(const WorkspaceId& workspace_id, InputMode mode) {
    std::scoped_lock lock(mutex_);
    if (!managed_workspace_id_.has_value() || *managed_workspace_id_ != workspace_id) {
        return false;
    }
    input_mode_ = mode;
    sync_committed_layout_snapshot_locked();
    return true;
}

void WorkspaceManager::note_internal_focus_request(const WorkspaceId& workspace_id, const ClientId& client_id) {
    std::scoped_lock lock(mutex_);
    prune_pending_internal_focus_requests_locked();
    const std::string normalized_client_id = normalize_client_id_for_query(client_id);
    if (last_active_client_id_.has_value() && *last_active_client_id_ == normalized_client_id) {
        return;
    }
    pending_internal_focus_requests_.push_back(PendingInternalFocusRequest {
        .workspace_id = workspace_id,
        .client_id = normalized_client_id,
        .deadline = clock_() + kPendingInternalFocusRequestTtl,
    });
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
    refresh_managing_emacs_client_locked();
    sync_committed_layout_snapshot_locked();
}

std::optional<WorkspaceId> WorkspaceManager::managed_workspace() const {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        return std::nullopt;
    }
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
    auto is_valid_managing_emacs = [&](const ClientId& client_id) {
        const auto* client = client_registry_.find(client_id);
        return client != nullptr && client->workspace_id == workspace_id && is_emacs_client(client->app_id, client->title);
    };

    if (managing_emacs_client_id_.has_value() && is_valid_managing_emacs(*managing_emacs_client_id_)) {
        return managing_emacs_client_id_;
    }
    return find_emacs_client_locked(workspace_id);
}

void WorkspaceManager::set_client_transition_notifier(ClientTransitionNotifier notifier) {
    std::scoped_lock lock(mutex_);
    client_transition_notifier_ = std::move(notifier);
}

void WorkspaceManager::set_state_change_notifier(StateChangeNotifier notifier) {
    std::scoped_lock lock(mutex_);
    state_change_notifier_ = std::move(notifier);
}

void WorkspaceManager::set_focus_request_notifier(FocusRequestNotifier notifier) {
    std::scoped_lock lock(mutex_);
    focus_request_notifier_ = std::move(notifier);
}

void WorkspaceManager::set_controller_connected(bool connected) {
    std::scoped_lock lock(mutex_);
    const bool transition_to_disconnected = controller_connected_ && !connected;
    controller_connected_ = connected;
    if (transition_to_disconnected) {
        managed_layout_snapshot_.reset();
        if (managed_workspace_id_.has_value()) {
            restore_managed_layout_locked(*managed_workspace_id_);
            managed_workspace_id_ = std::nullopt;
            input_mode_ = std::nullopt;
            last_active_client_id_ = std::nullopt;
            managing_emacs_client_id_ = std::nullopt;
            client_registry_.reconcile_management(managed_workspace_id_);
            managed_client_seen_.clear();
            overlay_float_pending_clients_.clear();
            clear_pending_internal_focus_requests_locked();
        }
    }
}

bool WorkspaceManager::apply_managed_layout_snapshot(ManagedWorkspaceLayoutSnapshot snapshot) {
    std::scoped_lock lock(mutex_);
    if (snapshot.workspace_id.empty() || !managed_workspace_id_.has_value() || *managed_workspace_id_ != snapshot.workspace_id) {
        return false;
    }

    refresh_managing_emacs_client_locked();
    managed_layout_version_ += 1;
    ManagedWorkspaceLayoutSnapshot committed_snapshot = std::move(snapshot);
    committed_snapshot.layout_version = managed_layout_version_;
    managed_layout_snapshot_ = std::move(committed_snapshot);
    sync_committed_layout_snapshot_locked();
    return true;
}

std::optional<ManagedWorkspaceLayoutSnapshot> WorkspaceManager::managed_layout_snapshot(const WorkspaceId& workspace_id) const {
    std::scoped_lock lock(mutex_);
    if (!managed_layout_snapshot_.has_value() || managed_layout_snapshot_->workspace_id != workspace_id) {
        return std::nullopt;
    }
    return managed_layout_snapshot_;
}

void WorkspaceManager::clear_managed_layout_snapshot(const WorkspaceId& workspace_id) {
    std::scoped_lock lock(mutex_);
    if (managed_layout_snapshot_.has_value() && managed_layout_snapshot_->workspace_id == workspace_id) {
        managed_layout_snapshot_.reset();
    }
}

void WorkspaceManager::note_overlay_float_request(const WorkspaceId& workspace_id, const ClientId& client_id) {
    std::scoped_lock lock(mutex_);
    if (!managed_layout_snapshot_.has_value() || managed_layout_snapshot_->workspace_id != workspace_id) {
        return;
    }
    if (!is_snapshot_visible_client_locked(client_id)) {
        return;
    }
    overlay_float_pending_clients_.insert(normalize_client_id_for_query(client_id));
}

bool WorkspaceManager::refresh_workspace_floating_state_from_query(
    const WorkspaceId& workspace_id, bool include_managed_clients
) {
    std::scoped_lock lock(mutex_);

    const bool state_mutated = refresh_workspace_floating_state_locked(workspace_id, include_managed_clients, true);

    if (state_mutated) {
        refresh_managing_emacs_client_locked();
        sync_committed_layout_snapshot_locked();
        log_state_dump_locked();
    }
    return state_mutated;
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

std::optional<int> WorkspaceManager::plugin_option_int(std::string_view option_name) const {
    return query_option_int_locked(option_name);
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

std::optional<std::string> WorkspaceManager::query_option_string_locked(std::string_view option_name) const {
    if (!query_executor_) {
        return std::nullopt;
    }
    auto response = query_executor_(std::string("j/getoption ") + std::string(option_name));
    if (!response.has_value()) {
        return std::nullopt;
    }
    return parse_json_string_field(*response, "str");
}

std::optional<WorkspaceManager::QueriedClientState> WorkspaceManager::query_client_state_locked(std::string_view client_id) const {
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

    const auto floating = parse_json_bool_field(*object_json, "floating");
    if (!floating.has_value()) {
        return std::nullopt;
    }

    return QueriedClientState {
        .floating = *floating,
        .in_internal_hidden_workspace = client_object_in_internal_hidden_workspace(*object_json),
    };
}

std::optional<bool> WorkspaceManager::query_client_floating_locked(std::string_view client_id) const {
    const auto state = query_client_state_locked(client_id);
    if (!state.has_value()) {
        return std::nullopt;
    }
    return state->floating;
}

std::optional<std::string> WorkspaceManager::query_workspace_tiled_layout_locked(std::string_view workspace_id) const {
    if (!query_executor_) {
        return std::nullopt;
    }

    auto response = query_executor_("j/workspaces");
    if (!response.has_value()) {
        return std::nullopt;
    }

    const auto workspace_object = find_workspace_object_json(*response, workspace_id);
    if (!workspace_object.has_value()) {
        return std::nullopt;
    }

    auto tiled_layout = parse_json_string_field(*workspace_object, "tiledLayout");
    if (!tiled_layout.has_value()) {
        tiled_layout = parse_json_string_field(*workspace_object, "tiledlayout");
    }
    return tiled_layout;
}

bool WorkspaceManager::dispatch_keyword_locked(std::string_view key, std::string_view value) const {
    if (!dispatch_executor_) {
        return false;
    }
    const std::string command = "keyword " + std::string(key) + " " + std::string(value);
    return dispatch_executor_(command) == 0;
}

std::optional<ClientId> WorkspaceManager::find_emacs_client_locked(const WorkspaceId& workspace_id) const {
    const auto snapshot = client_registry_.snapshot();
    for (const auto& client : snapshot.clients) {
        if (client.workspace_id != workspace_id) {
            continue;
        }
        if (is_emacs_client(client.app_id, client.title)) {
            return client.client_id;
        }
    }
    return std::nullopt;
}

std::optional<ClientId> WorkspaceManager::selected_managed_client_locked(const WorkspaceId& workspace_id) const {
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

bool WorkspaceManager::is_snapshot_visible_client_locked(std::string_view client_id) const {
    if (!managed_layout_snapshot_.has_value()) {
        return false;
    }

    const std::string normalized_client_id = normalize_client_id_for_query(client_id);
    return std::find(
               managed_layout_snapshot_->visible_client_ids.begin(),
               managed_layout_snapshot_->visible_client_ids.end(),
               normalized_client_id
           ) != managed_layout_snapshot_->visible_client_ids.end();
}

bool WorkspaceManager::is_snapshot_hidden_client_locked(std::string_view client_id) const {
    if (!managed_layout_snapshot_.has_value()) {
        return false;
    }

    const std::string normalized_client_id = normalize_client_id_for_query(client_id);
    return std::find(
               managed_layout_snapshot_->hidden_client_ids.begin(),
               managed_layout_snapshot_->hidden_client_ids.end(),
               normalized_client_id
           ) != managed_layout_snapshot_->hidden_client_ids.end();
}

bool WorkspaceManager::should_ignore_overlay_floating_update_locked(
    std::string_view client_id, bool floating, bool in_internal_hidden_workspace, FloatingUpdateSource source
) {
    const std::string normalized_client_id = normalize_client_id_for_query(client_id);
    if (!floating) {
        overlay_float_pending_clients_.erase(normalized_client_id);
        return false;
    }
    if (in_internal_hidden_workspace && is_snapshot_hidden_client_locked(normalized_client_id)) {
        return true;
    }
    if (!is_snapshot_visible_client_locked(normalized_client_id)) {
        return false;
    }
    if (source == FloatingUpdateSource::PassiveQuery) {
        overlay_float_pending_clients_.erase(normalized_client_id);
        return true;
    }

    const auto pending_it = overlay_float_pending_clients_.find(normalized_client_id);
    if (pending_it != overlay_float_pending_clients_.end()) {
        overlay_float_pending_clients_.erase(pending_it);
        return true;
    }

    return false;
}

bool WorkspaceManager::consume_internal_focus_request_locked(const WorkspaceId& workspace_id, std::string_view client_id) {
    prune_pending_internal_focus_requests_locked();
    const std::string normalized_client_id = normalize_client_id_for_query(client_id);
    const auto it = std::find_if(
        pending_internal_focus_requests_.begin(),
        pending_internal_focus_requests_.end(),
        [&](const auto& pending) {
            return pending.workspace_id == workspace_id && pending.client_id == normalized_client_id;
        }
    );
    if (it == pending_internal_focus_requests_.end()) {
        return false;
    }

    pending_internal_focus_requests_.erase(it);
    return true;
}

void WorkspaceManager::clear_pending_internal_focus_requests_locked() {
    pending_internal_focus_requests_.clear();
}

void WorkspaceManager::prune_pending_internal_focus_requests_for_client_locked(std::string_view client_id) {
    const std::string normalized_client_id = normalize_client_id_for_query(client_id);
    pending_internal_focus_requests_.erase(
        std::remove_if(
            pending_internal_focus_requests_.begin(),
            pending_internal_focus_requests_.end(),
            [&](const auto& pending) {
                return pending.client_id == normalized_client_id;
            }
        ),
        pending_internal_focus_requests_.end()
    );
}

void WorkspaceManager::prune_pending_internal_focus_requests_locked() {
    const auto now = clock_();
    pending_internal_focus_requests_.erase(
        std::remove_if(
            pending_internal_focus_requests_.begin(),
            pending_internal_focus_requests_.end(),
            [&](const auto& pending) {
                if (pending.deadline <= now) {
                    return true;
                }
                if (!managed_workspace_id_.has_value() || pending.workspace_id != *managed_workspace_id_) {
                    return true;
                }
                const auto* client = client_registry_.find(pending.client_id);
                return client == nullptr || client->workspace_id != pending.workspace_id || !client->managed ||
                       client->floating || !client->eligible;
            }
        ),
        pending_internal_focus_requests_.end()
    );
}

bool WorkspaceManager::refresh_workspace_floating_state_locked(
    const WorkspaceId& workspace_id, bool include_managed_clients, bool discover_missing_clients
) {
    bool state_mutated = false;

    if (discover_missing_clients && query_executor_) {
        auto response = query_executor_("j/clients");
        if (response.has_value()) {
            for (const auto object_json : client_objects_json(*response)) {
                const auto address = parse_json_string_field(object_json, "address");
                const auto queried_workspace_id = parse_json_int_field(object_json, "id");
                const auto app_id = parse_json_string_field(object_json, "class");
                const auto title = parse_json_string_field(object_json, "title");
                const auto floating = parse_json_bool_field(object_json, "floating");
                if (!address.has_value() || !queried_workspace_id.has_value() || !app_id.has_value() ||
                    !title.has_value() || !floating.has_value() ||
                    std::to_string(*queried_workspace_id) != workspace_id) {
                    continue;
                }

                const std::string normalized_client_id = normalize_client_id_for_query(*address);
                if (client_registry_.find(normalized_client_id) != nullptr) {
                    continue;
                }
                client_registry_.upsert_open(normalized_client_id, workspace_id, *app_id, *title);
                client_registry_.set_floating(normalized_client_id, *floating);
                state_mutated = true;
            }
            if (state_mutated) {
                client_registry_.reconcile_management(managed_workspace_id_);
            }
        }
    }

    const auto snapshot = client_registry_.snapshot();
    for (const auto& client : snapshot.clients) {
        if (client.workspace_id != workspace_id) {
            continue;
        }
        if (!include_managed_clients && client.managed) {
            continue;
        }

        const auto queried_state = query_client_state_locked(client.client_id);
        if (!queried_state.has_value()) {
            continue;
        }
        if (should_ignore_overlay_floating_update_locked(
                client.client_id,
                queried_state->floating,
                queried_state->in_internal_hidden_workspace,
                FloatingUpdateSource::ExplicitQuery
            )) {
            continue;
        }
        if (queried_state->floating == client.floating) {
            continue;
        }

        client_registry_.set_floating(client.client_id, queried_state->floating);
        client_registry_.reconcile_management(managed_workspace_id_);
        state_mutated = true;

        const auto* after = client_registry_.find(client.client_id);
        if (after == nullptr || !managed_workspace_id_.has_value() || after->workspace_id != *managed_workspace_id_ ||
            !after->managed) {
            managed_client_seen_.erase(normalize_client_id_for_query(client.client_id));
        } else {
            managed_client_seen_.insert(after->client_id);
        }
    }

    return state_mutated;
}

void WorkspaceManager::sync_committed_layout_snapshot_locked() {
    if (!managed_layout_snapshot_.has_value() || !managed_workspace_id_.has_value() ||
        managed_layout_snapshot_->workspace_id != *managed_workspace_id_) {
        return;
    }

    const auto snapshot = client_registry_.snapshot();
    std::unordered_set<ClientId> active_managed_client_ids;
    for (const auto& client : snapshot.clients) {
        if (client.workspace_id == *managed_workspace_id_ && client.managed && client.eligible) {
            active_managed_client_ids.insert(client.client_id);
        }
    }

    auto& committed_snapshot = *managed_layout_snapshot_;
    for (auto it = committed_snapshot.rectangles_by_client_id.begin(); it != committed_snapshot.rectangles_by_client_id.end();) {
        if (active_managed_client_ids.find(it->first) == active_managed_client_ids.end()) {
            it = committed_snapshot.rectangles_by_client_id.erase(it);
        } else {
            ++it;
        }
    }

    auto prune_client_vector = [&](std::vector<ClientId>& client_ids) {
        client_ids.erase(
            std::remove_if(client_ids.begin(), client_ids.end(), [&](const ClientId& client_id) {
                return active_managed_client_ids.find(client_id) == active_managed_client_ids.end();
            }),
            client_ids.end()
        );
    };

    prune_client_vector(committed_snapshot.visible_client_ids);
    prune_client_vector(committed_snapshot.hidden_client_ids);
    prune_client_vector(committed_snapshot.stacking_order);

    auto snapshot_contains_client = [&](const ClientId& client_id) {
        return committed_snapshot.rectangles_by_client_id.find(client_id) != committed_snapshot.rectangles_by_client_id.end() ||
               std::find(committed_snapshot.visible_client_ids.begin(), committed_snapshot.visible_client_ids.end(), client_id) !=
                   committed_snapshot.visible_client_ids.end() ||
               std::find(committed_snapshot.hidden_client_ids.begin(), committed_snapshot.hidden_client_ids.end(), client_id) !=
                   committed_snapshot.hidden_client_ids.end() ||
               std::find(committed_snapshot.stacking_order.begin(), committed_snapshot.stacking_order.end(), client_id) !=
                   committed_snapshot.stacking_order.end();
    };

    for (const auto& client_id : active_managed_client_ids) {
        if (!snapshot_contains_client(client_id)) {
            committed_snapshot.hidden_client_ids.push_back(client_id);
        }
    }

    managed_layout_snapshot_->selected_client = selected_managed_client_locked(*managed_workspace_id_);
    managed_layout_snapshot_->input_mode = input_mode_;
    managed_layout_snapshot_->managing_emacs_client_id = managing_emacs_client_id_;
}

void WorkspaceManager::refresh_managing_emacs_client_locked() {
    if (!managed_workspace_id_.has_value()) {
        managing_emacs_client_id_ = std::nullopt;
        sync_committed_layout_snapshot_locked();
        return;
    }

    auto is_valid_for_managed_workspace = [&](const ClientId& client_id) {
        const auto* client = client_registry_.find(client_id);
        return client != nullptr && client->workspace_id == *managed_workspace_id_ &&
               is_emacs_client(client->app_id, client->title);
    };

    if (managing_emacs_client_id_.has_value() && is_valid_for_managed_workspace(*managing_emacs_client_id_)) {
        return;
    }

    if (last_active_client_id_.has_value() && is_valid_for_managed_workspace(*last_active_client_id_)) {
        managing_emacs_client_id_ = *last_active_client_id_;
        sync_committed_layout_snapshot_locked();
        return;
    }

    const auto snapshot = client_registry_.snapshot();
    if (snapshot.selected_client.has_value() && is_valid_for_managed_workspace(*snapshot.selected_client)) {
        managing_emacs_client_id_ = *snapshot.selected_client;
        sync_committed_layout_snapshot_locked();
        return;
    }

    managing_emacs_client_id_ = find_emacs_client_locked(*managed_workspace_id_);
    sync_committed_layout_snapshot_locked();
}

void WorkspaceManager::apply_managed_layout_locked(const WorkspaceId& workspace_id) {
    if (!workspace_layout_snapshot_.contains(workspace_id)) {
        auto previous_layout = query_workspace_tiled_layout_locked(workspace_id);
        if (!previous_layout.has_value()) {
            previous_layout = query_option_string_locked("general:layout");
        }
        workspace_layout_snapshot_[workspace_id] = previous_layout.value_or("dwindle");
    }

    (void)dispatch_keyword_locked("workspace", workspace_id + ",layout:hyprmacs");
    acquire_managed_policy_lease_locked();
}

void WorkspaceManager::restore_managed_layout_locked(const WorkspaceId& workspace_id) {
    const auto it = workspace_layout_snapshot_.find(workspace_id);
    const std::string restore_layout = (it != workspace_layout_snapshot_.end()) ? it->second : "dwindle";

    (void)dispatch_keyword_locked("workspace", workspace_id + ",layout:" + restore_layout);
    if (it != workspace_layout_snapshot_.end()) {
        workspace_layout_snapshot_.erase(it);
    }

    release_managed_policy_lease_locked();
}

void WorkspaceManager::capture_policy_snapshot_locked() {
    policy_snapshot_.animations_enabled = query_option_int_locked("animations:enabled");
    policy_snapshot_.focus_on_activate = query_option_int_locked("misc:focus_on_activate");
}

void WorkspaceManager::acquire_managed_policy_lease_locked() {
    if (policy_lease_count_ == 0) {
        capture_policy_snapshot_locked();
        (void)dispatch_keyword_locked("animations:enabled", "0");
        (void)dispatch_keyword_locked("misc:focus_on_activate", "0");
    }
    ++policy_lease_count_;
}

void WorkspaceManager::release_managed_policy_lease_locked() {
    if (policy_lease_count_ == 0) {
        return;
    }

    --policy_lease_count_;
    if (policy_lease_count_ == 0) {
        if (policy_snapshot_.animations_enabled.has_value()) {
            (void)dispatch_keyword_locked("animations:enabled", std::to_string(*policy_snapshot_.animations_enabled));
        }
        if (policy_snapshot_.focus_on_activate.has_value()) {
            (void)dispatch_keyword_locked("misc:focus_on_activate", std::to_string(*policy_snapshot_.focus_on_activate));
        }
        policy_snapshot_ = PolicySnapshot {};
    }
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
    bool state_change_notify = false;
    WorkspaceId transition_workspace;
    ClientId transition_client_id;
    ClientTransitionNotifier transition_notifier;
    WorkspaceId state_change_workspace;
    StateChangeNotifier state_change_notifier;
    bool focus_request_notify = false;
    WorkspaceId focus_request_workspace;
    ClientId focus_request_client_id;
    FocusRequestNotifier focus_request_notifier;

    {
        std::scoped_lock lock(mutex_);
        event_counts_[frame->name] += 1;
        auto maybe_set_transition = [&](std::string_view raw_client_id, bool floating_value) {
            const auto after = client_registry_.find(std::string(raw_client_id));
            if (after == nullptr || !managed_workspace_id_.has_value()) {
                return;
            }
            const bool can_notify = client_transition_notifier_ != nullptr && after->workspace_id == *managed_workspace_id_;
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
        auto sync_managed_seen_for_client = [&](std::string_view raw_client_id) {
            const auto normalized_client_id = normalize_client_id_for_query(raw_client_id);
            const auto after = client_registry_.find(std::string(raw_client_id));
            if (after == nullptr || !managed_workspace_id_.has_value() || after->workspace_id != *managed_workspace_id_ ||
                !after->managed) {
                managed_client_seen_.erase(normalized_client_id);
                return;
            }

            const auto [_, inserted] = managed_client_seen_.insert(after->client_id);
            if (inserted) {
                maybe_set_transition(after->client_id, false);
            }
        };
        auto refresh_floating_from_query = [&](std::string_view raw_client_id) {
            const auto before = client_registry_.find(std::string(raw_client_id));
            if (before == nullptr) {
                return;
            }
            const bool was_managed = before->managed;
            const auto queried_state = query_client_state_locked(raw_client_id);
            if (!queried_state.has_value() || queried_state->floating == before->floating) {
                return;
            }
            if (should_ignore_overlay_floating_update_locked(
                    raw_client_id,
                    queried_state->floating,
                    queried_state->in_internal_hidden_workspace,
                    FloatingUpdateSource::PassiveQuery
                )) {
                return;
            }

            client_registry_.set_floating(std::string(raw_client_id), queried_state->floating);
            client_registry_.reconcile_management(managed_workspace_id_);
            state_mutated = true;

            const auto after = client_registry_.find(std::string(raw_client_id));
            const bool now_managed = after != nullptr && after->managed;
            if (queried_state->floating && was_managed && !now_managed) {
                maybe_set_transition(raw_client_id, true);
            } else if (!queried_state->floating && !was_managed && now_managed) {
                maybe_set_transition(raw_client_id, false);
            }
            sync_managed_seen_for_client(raw_client_id);
        };
        auto maybe_set_focus_request = [&](std::string_view raw_client_id) {
            if (focus_request_notify || focus_request_notifier_ == nullptr || !managed_workspace_id_.has_value()) {
                return;
            }
            const auto* focused = client_registry_.find(std::string(raw_client_id));
            if (focused == nullptr || focused->workspace_id != *managed_workspace_id_ || !focused->managed ||
                focused->floating || !focused->eligible) {
                return;
            }
            if (consume_internal_focus_request_locked(*managed_workspace_id_, focused->client_id)) {
                return;
            }
            focus_request_notify = true;
            focus_request_workspace = *managed_workspace_id_;
            focus_request_client_id = focused->client_id;
            focus_request_notifier = focus_request_notifier_;
        };

        if (frame->name == "openwindow" || frame->name == "openwindowv2") {
            const auto parts = split_csv_limited(frame->payload, 4);
            if (parts.size() == 4) {
                client_registry_.upsert_open(parts[0], parts[1], parts[2], parts[3]);
                client_registry_.reconcile_management(managed_workspace_id_);
                refresh_floating_from_query(parts[0]);
                sync_managed_seen_for_client(parts[0]);
                state_mutated = true;
            }
        } else if (frame->name == "closewindow") {
            const auto parts = split_csv_limited(frame->payload, 2);
            if (!parts.empty()) {
                const std::string normalized_id = normalize_client_id_for_query(parts[0]);
                client_registry_.erase(parts[0]);
                if (last_active_client_id_.has_value() && *last_active_client_id_ == normalized_id) {
                    last_active_client_id_ = std::nullopt;
                }
                managed_client_seen_.erase(normalize_client_id_for_query(parts[0]));
                overlay_float_pending_clients_.erase(normalize_client_id_for_query(parts[0]));
                prune_pending_internal_focus_requests_for_client_locked(parts[0]);
                client_registry_.reconcile_management(managed_workspace_id_);
                state_mutated = true;
            }
        } else if (frame->name == "movewindow" || frame->name == "movewindowv2") {
            const auto parts = split_csv_limited(frame->payload, 3);
            if (parts.size() >= 2) {
                if (!is_internal_hidden_workspace_target(parts[1])) {
                    client_registry_.update_workspace(parts[0], parts[1]);
                    client_registry_.reconcile_management(managed_workspace_id_);
                    sync_managed_seen_for_client(parts[0]);
                    state_mutated = true;
                }
            }
        } else if (frame->name == "activewindowv2") {
            const auto parts = split_csv_limited(frame->payload, 2);
            if (!parts.empty()) {
                last_active_client_id_ = normalize_client_id_for_query(parts[0]);
                client_registry_.set_focus(parts[0]);
                refresh_floating_from_query(parts[0]);
                sync_managed_seen_for_client(parts[0]);
                maybe_set_focus_request(parts[0]);
                state_mutated = true;
            }
        } else if (frame->name == "windowtitlev2") {
            const auto parts = split_csv_limited(frame->payload, 2);
            if (parts.size() == 2) {
                client_registry_.update_title(parts[0], parts[1]);
                client_registry_.reconcile_management(managed_workspace_id_);
                refresh_floating_from_query(parts[0]);
                sync_managed_seen_for_client(parts[0]);
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
                    if (should_ignore_overlay_floating_update_locked(
                            parts[0], *floating, false, FloatingUpdateSource::Event
                        )) {
                        sync_managed_seen_for_client(parts[0]);
                    } else {
                        client_registry_.set_floating(parts[0], *floating);
                        client_registry_.reconcile_management(managed_workspace_id_);
                        state_mutated = true;

                        const auto after = client_registry_.find(parts[0]);
                        const bool now_managed = after != nullptr && after->managed;
                        const bool can_notify = managed_workspace_id_.has_value() && client_transition_notifier_ != nullptr &&
                                                after != nullptr && after->workspace_id == *managed_workspace_id_;
                        if (can_notify) {
                            if (*floating && was_managed && !now_managed) {
                                maybe_set_transition(parts[0], true);
                            } else if (!*floating && !was_managed && now_managed) {
                                maybe_set_transition(parts[0], false);
                            }
                        }
                        sync_managed_seen_for_client(parts[0]);
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
            refresh_managing_emacs_client_locked();
            sync_committed_layout_snapshot_locked();
            log_state_dump_locked();
            if (!transition_notify && controller_connected_ && managed_workspace_id_.has_value() && state_change_notifier_ != nullptr) {
                state_change_notify = true;
                state_change_workspace = *managed_workspace_id_;
                state_change_notifier = state_change_notifier_;
            }
        }
    }

    if (transition_notify && transition_notifier != nullptr) {
        transition_notifier(transition_workspace, transition_client_id, transition_floating);
    }
    if (state_change_notify && state_change_notifier != nullptr) {
        state_change_notifier(state_change_workspace);
    }
    if (focus_request_notify && focus_request_notifier != nullptr) {
        focus_request_notifier(focus_request_workspace, focus_request_client_id);
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
              << " selected=" << (snapshot.selected_client.has_value() ? *snapshot.selected_client : "none")
              << " managing-emacs=" << (managing_emacs_client_id_.has_value() ? *managing_emacs_client_id_ : "none")
              << '\n';

    for (const auto& client : snapshot.clients) {
        std::cerr << "[hyprmacs] state-dump client id=" << client.client_id << " ws=" << client.workspace_id
                  << " app=" << client.app_id << " floating=" << (client.floating ? "true" : "false")
                  << " eligible=" << (client.eligible ? "true" : "false") << " selected="
                  << (client.selected ? "true" : "false") << " title=" << client.title << '\n';
    }
}

}  // namespace hyprmacs
