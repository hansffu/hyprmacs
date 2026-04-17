#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "hyprmacs/client_registry.hpp"
#include "hyprmacs/plugin_state.hpp"

namespace hyprmacs {

struct EventFrame {
    std::string name;
    std::string payload;
};

std::optional<EventFrame> parse_event_frame(const std::string& line);
bool is_tracked_event_name(std::string_view event_name);

class WorkspaceManager {
  public:
    using DispatchExecutor = std::function<int(const std::string&)>;
    using QueryExecutor = std::function<std::optional<std::string>(const std::string&)>;
    using ClientTransitionNotifier = std::function<void(const WorkspaceId&, const ClientId&, bool)>;

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
    bool set_selected_client(const WorkspaceId& workspace_id, const ClientId& client_id);
    bool set_input_mode(const WorkspaceId& workspace_id, InputMode mode);
    void seed_client(const ClientId& client_id, const WorkspaceId& workspace_id, const std::string& app_id,
                     const std::string& title, bool floating);
    std::optional<ClientId> selected_managed_client(const WorkspaceId& workspace_id) const;
    std::optional<ClientId> emacs_client(const WorkspaceId& workspace_id) const;
    void set_client_transition_notifier(ClientTransitionNotifier notifier);
    void set_controller_connected(bool connected);
    StateDumpPayload build_state_dump(const WorkspaceId& workspace_id) const;
    void process_event_for_tests(const std::string& line);

  private:
    struct PolicySnapshot {
        std::optional<int> follow_mouse;
        std::optional<int> animations_enabled;
        std::optional<int> focus_on_activate;
    };

    static std::optional<int> parse_int_field(std::string_view json, std::string_view key);
    std::optional<int> query_option_int_locked(std::string_view option_name) const;
    std::optional<bool> query_client_floating_locked(std::string_view client_id) const;
    bool dispatch_keyword_locked(std::string_view key, std::string_view value) const;
    void capture_policy_snapshot_locked();
    void apply_managed_policy_locked();
    void restore_policy_locked();

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
    bool controller_connected_ = false;
    bool policy_applied_ = false;
    PolicySnapshot policy_snapshot_;
    DispatchExecutor dispatch_executor_;
    QueryExecutor query_executor_;
    ClientTransitionNotifier client_transition_notifier_;
};

}  // namespace hyprmacs
