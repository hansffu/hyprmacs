#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "hyprmacs/protocol.hpp"
#include "hyprmacs/workspace_manager.hpp"

namespace hyprmacs {

std::optional<std::string> default_ipc_socket_path();
std::vector<ProtocolMessage> route_command_for_tests(const ProtocolMessage& incoming, WorkspaceManager& workspace_manager);

class IpcServer {
  public:
    explicit IpcServer(WorkspaceManager* workspace_manager);
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

    WorkspaceManager* workspace_manager_ = nullptr;
    std::optional<std::string> socket_path_;

    std::atomic<bool> running_ {false};
    int listen_fd_ = -1;
    std::thread accept_thread_;
};

}  // namespace hyprmacs
