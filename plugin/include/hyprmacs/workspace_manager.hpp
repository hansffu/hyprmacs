#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "hyprmacs/client_registry.hpp"

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
};

}  // namespace hyprmacs
