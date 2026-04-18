#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "hyprmacs/focus_controller.hpp"
#include "hyprmacs/layout_applier.hpp"
#include "hyprmacs/protocol.hpp"
#include "hyprmacs/workspace_manager.hpp"

namespace hyprmacs {

std::optional<std::string> default_ipc_socket_path();
std::vector<ProtocolMessage> route_command_for_tests(
    const ProtocolMessage& incoming,
    WorkspaceManager& workspace_manager,
    LayoutApplier& layout_applier,
    FocusController* focus_controller = nullptr
);

class IpcServer {
  public:
    IpcServer(WorkspaceManager* workspace_manager, LayoutApplier* layout_applier, FocusController* focus_controller);
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start();
    void stop();

    std::optional<std::string> socket_path() const;

  private:
    void accept_loop();
    void serve_controller(int controller_fd);
    void send_message(int fd, const ProtocolMessage& message);
    void restore_workspace_on_disconnect();
    void on_client_transition(const WorkspaceId& workspace_id, const ClientId& client_id, bool floating);

    WorkspaceManager* workspace_manager_ = nullptr;
    LayoutApplier* layout_applier_ = nullptr;
    FocusController* focus_controller_ = nullptr;
    std::optional<std::string> socket_path_;

    std::atomic<bool> running_ {false};
    int listen_fd_ = -1;
    int controller_fd_ = -1;
    std::thread accept_thread_;
    mutable std::mutex controller_mutex_;
    mutable std::mutex send_mutex_;
};

}  // namespace hyprmacs
