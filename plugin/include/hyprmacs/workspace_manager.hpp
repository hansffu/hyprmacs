#pragma once

#include <cstdint>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "hyprmacs/client_registry.hpp"
#include "hyprmacs/client_classifier.hpp"
#include "hyprmacs/plugin_state.hpp"

namespace hyprmacs {

struct EventFrame {
    std::string name;
    std::string payload;
};

std::optional<EventFrame> parse_event_frame(const std::string& line);
bool is_tracked_event_name(std::string_view event_name);

struct ManagedWorkspaceLayoutSnapshot {
    WorkspaceId workspace_id;
    std::uint64_t layout_version = 0;
    std::unordered_map<ClientId, ClientRect> rectangles_by_client_id;
    std::vector<ClientId> visible_client_ids;
    std::vector<ClientId> hidden_client_ids;
    std::vector<ClientId> stacking_order;
    std::optional<ClientId> selected_client;
    std::optional<InputMode> input_mode;
    std::optional<ClientId> managing_emacs_client_id;
};

struct SummonCandidate {
    ClientId client_id;
    WorkspaceId workspace_id;
    std::string app_id;
    std::string title;
};

class WorkspaceManager {
  public:
    using DispatchExecutor = std::function<int(const std::string&)>;
    using QueryExecutor = std::function<std::optional<std::string>(const std::string&)>;
    using ClientTransitionNotifier = std::function<void(const WorkspaceId&, const ClientId&, bool)>;
    using StateChangeNotifier = std::function<void(const WorkspaceId&)>;
    using FocusRequestNotifier = std::function<void(const WorkspaceId&, const ClientId&)>;

    WorkspaceManager();
    explicit WorkspaceManager(DispatchExecutor dispatch_executor, QueryExecutor query_executor = {});
    ~WorkspaceManager();

    WorkspaceManager(const WorkspaceManager&) = delete;
    WorkspaceManager& operator=(const WorkspaceManager&) = delete;

    bool start_event_tap();
    void stop_event_tap();

    std::unordered_map<std::string, size_t> event_counts() const;

    bool manage_workspace(const WorkspaceId& workspace_id);
    bool unmanage_workspace(const WorkspaceId& workspace_id);
    bool can_float_managed_client(const WorkspaceId& workspace_id, const ClientId& client_id) const;
    bool float_managed_client(const WorkspaceId& workspace_id, const ClientId& client_id);
    std::vector<SummonCandidate> summon_candidates(const WorkspaceId& target_workspace_id) const;
    bool summon_client(const WorkspaceId& target_workspace_id, const ClientId& client_id);
    bool set_selected_client(const WorkspaceId& workspace_id, const ClientId& client_id);
    bool set_input_mode(const WorkspaceId& workspace_id, InputMode mode);
    void seed_client(const ClientId& client_id, const WorkspaceId& workspace_id, const std::string& app_id,
                     const std::string& title, bool floating);
    std::optional<WorkspaceId> managed_workspace() const;
    std::optional<ClientId> selected_managed_client(const WorkspaceId& workspace_id) const;
    std::optional<ClientId> emacs_client(const WorkspaceId& workspace_id) const;
    void note_internal_focus_request(const WorkspaceId& workspace_id, const ClientId& client_id);
    void set_client_transition_notifier(ClientTransitionNotifier notifier);
    void set_state_change_notifier(StateChangeNotifier notifier);
    void set_focus_request_notifier(FocusRequestNotifier notifier);
    void set_controller_connected(bool connected);
    bool apply_managed_layout_snapshot(ManagedWorkspaceLayoutSnapshot snapshot);
    std::optional<ManagedWorkspaceLayoutSnapshot> managed_layout_snapshot(const WorkspaceId& workspace_id) const;
    void clear_managed_layout_snapshot(const WorkspaceId& workspace_id);
    void note_overlay_float_request(const WorkspaceId& workspace_id, const ClientId& client_id);
    bool refresh_workspace_floating_state_from_query(const WorkspaceId& workspace_id, bool include_managed_clients = true);
    StateDumpPayload build_state_dump(const WorkspaceId& workspace_id) const;
    std::optional<int> plugin_option_int(std::string_view option_name) const;
    void process_event_for_tests(const std::string& line);

  private:
    struct PolicySnapshot {
        std::optional<int> animations_enabled;
        std::optional<int> focus_on_activate;
    };
    struct QueriedClientState {
        bool floating = false;
        bool in_internal_hidden_workspace = false;
    };
    enum class FloatingUpdateSource {
        Event,
        PassiveQuery,
        ExplicitQuery,
    };

    static std::optional<int> parse_int_field(std::string_view json, std::string_view key);
    std::optional<int> query_option_int_locked(std::string_view option_name) const;
    std::optional<std::string> query_option_string_locked(std::string_view option_name) const;
    std::optional<QueriedClientState> query_client_state_locked(std::string_view client_id) const;
    std::optional<bool> query_client_floating_locked(std::string_view client_id) const;
    std::optional<std::string> query_workspace_tiled_layout_locked(std::string_view workspace_id) const;
    bool dispatch_keyword_locked(std::string_view key, std::string_view value) const;
    std::optional<ClientId> find_emacs_client_locked(const WorkspaceId& workspace_id) const;
    std::optional<ClientId> selected_managed_client_locked(const WorkspaceId& workspace_id) const;
    bool is_snapshot_visible_client_locked(std::string_view client_id) const;
    bool is_snapshot_hidden_client_locked(std::string_view client_id) const;
    bool should_ignore_overlay_floating_update_locked(std::string_view client_id, bool floating,
                                                      bool in_internal_hidden_workspace, FloatingUpdateSource source);
    void clear_pending_internal_focus_requests_locked();
    void prune_pending_internal_focus_requests_for_client_locked(std::string_view client_id);
    void prune_pending_internal_focus_requests_locked();
    bool consume_internal_focus_request_locked(const WorkspaceId& workspace_id, std::string_view client_id);
    bool refresh_workspace_floating_state_locked(const WorkspaceId& workspace_id, bool include_managed_clients);
    void sync_committed_layout_snapshot_locked();
    void refresh_managing_emacs_client_locked();

    void apply_managed_layout_locked(const WorkspaceId& workspace_id);
    void restore_managed_layout_locked(const WorkspaceId& workspace_id);
    void capture_policy_snapshot_locked();
    void acquire_managed_policy_lease_locked();
    void release_managed_policy_lease_locked();

    void event_loop();
    void handle_line(const std::string& line);
    bool should_track(const std::string& event_name) const;
    void log_state_dump_locked() const;

    std::atomic<bool> running_ {false};
    std::thread event_thread_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, size_t> event_counts_;
    ClientRegistry client_registry_;
    std::optional<WorkspaceId> managed_workspace_id_;
    std::optional<InputMode> input_mode_;
    std::optional<ClientId> last_active_client_id_;
    bool controller_connected_ = false;
    size_t policy_lease_count_ = 0;
    PolicySnapshot policy_snapshot_;
    std::unordered_map<WorkspaceId, std::string> workspace_layout_snapshot_;
    std::unordered_set<ClientId> managed_client_seen_;
    std::unordered_set<ClientId> overlay_float_pending_clients_;
    std::vector<std::pair<WorkspaceId, ClientId>> pending_internal_focus_requests_;
    std::optional<ClientId> managing_emacs_client_id_;
    std::optional<ManagedWorkspaceLayoutSnapshot> managed_layout_snapshot_;
    std::uint64_t managed_layout_version_ = 0;
    DispatchExecutor dispatch_executor_;
    QueryExecutor query_executor_;
    ClientTransitionNotifier client_transition_notifier_;
    StateChangeNotifier state_change_notifier_;
    FocusRequestNotifier focus_request_notifier_;
};

}  // namespace hyprmacs
