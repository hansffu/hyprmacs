#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "hyprmacs/focus_controller.hpp"
#include "hyprmacs/layout_applier.hpp"
#include "hyprmacs/protocol.hpp"
#include "hyprmacs/workspace_manager.hpp"

namespace hyprmacs {

using RecalcRequester = std::function<bool(const WorkspaceId&)>;

int normalize_state_notify_debounce_ms(std::optional<int> configured_value, bool* used_default_out = nullptr,
                                       bool* clamped_out = nullptr);

std::optional<std::string> default_ipc_socket_path();
std::vector<ProtocolMessage> route_command_for_tests(
    const ProtocolMessage& incoming,
    WorkspaceManager& workspace_manager,
    LayoutApplier& layout_applier,
    FocusController* focus_controller = nullptr,
    RecalcRequester recalc_requester = {}
);
ProtocolMessage focus_request_message(const WorkspaceId& workspace_id, const ClientId& client_id);
bool controller_send_target_is_current(int candidate_fd, std::uint64_t candidate_generation, int current_fd,
                                       std::uint64_t current_generation);
enum class SendProtocolResult {
    Sent,
    WouldBlock,
    Partial,
    Failed,
};
SendProtocolResult send_protocol_message(int fd, const ProtocolMessage& message, int flags = 0);

class IpcServer {
  public:
    using RecalcRequester = hyprmacs::RecalcRequester;

    IpcServer(WorkspaceManager* workspace_manager, LayoutApplier* layout_applier, FocusController* focus_controller,
              RecalcRequester recalc_requester = {});
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start();
    void stop();

    std::optional<std::string> socket_path() const;
    void publish_state_dump_for_workspace(const WorkspaceId& workspace_id);

  private:
    void accept_loop();
    void serve_controller(int controller_fd);
    void send_message(int fd, const ProtocolMessage& message);
    void send_message_unlocked(int fd, const ProtocolMessage& message);
    void restore_workspace_on_disconnect();
    void on_client_transition(const WorkspaceId& workspace_id, const ClientId& client_id, bool floating);
    void on_workspace_state_changed(const WorkspaceId& workspace_id);
    void on_focus_request(const WorkspaceId& workspace_id, const ClientId& client_id);
    int resolve_state_notify_debounce_ms() const;
    void enqueue_debounced_state_dump(const WorkspaceId& workspace_id);
    void publish_state_dump_now(const WorkspaceId& workspace_id, std::optional<std::uint64_t> expected_generation = std::nullopt);
    void state_notify_loop();

    struct PendingStateNotification {
        std::chrono::steady_clock::time_point deadline;
        std::uint64_t controller_generation = 0;
    };

    WorkspaceManager* workspace_manager_ = nullptr;
    LayoutApplier* layout_applier_ = nullptr;
    FocusController* focus_controller_ = nullptr;
    RecalcRequester recalc_requester_;
    std::optional<std::string> socket_path_;
    int state_notify_debounce_ms_ = 30;

    std::atomic<bool> running_ {false};
    int listen_fd_ = -1;
    int controller_fd_ = -1;
    std::thread accept_thread_;
    std::thread state_notify_thread_;
    mutable std::mutex controller_mutex_;
    std::uint64_t controller_generation_ = 0;
    mutable std::mutex send_mutex_;
    mutable std::mutex state_notify_mutex_;
    std::condition_variable state_notify_cv_;
    std::unordered_map<WorkspaceId, PendingStateNotification> pending_state_notify_deadlines_;
};

}  // namespace hyprmacs
