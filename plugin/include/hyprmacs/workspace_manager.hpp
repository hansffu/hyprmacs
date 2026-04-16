#pragma once

#include <atomic>
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
    WorkspaceManager();
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
    void set_controller_connected(bool connected);
    StateDumpPayload build_state_dump(const WorkspaceId& workspace_id) const;
    void process_event_for_tests(const std::string& line);

  private:
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
};

}  // namespace hyprmacs
