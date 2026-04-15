#include "hyprmacs/ipc_server.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace hyprmacs {
namespace {

std::string now_rfc3339() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm {};
    gmtime_r(&t, &tm);

    char buffer[32] {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string payload_for_workspace_managed(bool controller_connected) {
    std::ostringstream out;
    out << "{";
    out << "\"managed\":true,";
    out << "\"controller_connected\":" << (controller_connected ? "true" : "false");
    out << "}";
    return out.str();
}

std::string payload_for_workspace_unmanaged(std::string_view reason) {
    std::ostringstream out;
    out << "{";
    out << "\"managed\":false,";
    out << "\"reason\":\"" << reason << "\"";
    out << "}";
    return out.str();
}

ProtocolMessage make_message(std::string_view type, const WorkspaceId& workspace_id, std::string payload_json) {
    return ProtocolMessage {
        .version = 1,
        .type = std::string(type),
        .workspace_id = workspace_id,
        .timestamp = now_rfc3339(),
        .payload_json = std::move(payload_json),
    };
}

}  // namespace

std::optional<std::string> default_ipc_socket_path() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir == nullptr || *runtime_dir == '\0') {
        return std::nullopt;
    }

    std::string path = runtime_dir;
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    path += "hyprmacs-v1.sock";
    return path;
}

std::vector<ProtocolMessage> route_command_for_tests(const ProtocolMessage& incoming, WorkspaceManager& workspace_manager) {
    std::vector<ProtocolMessage> out;

    if (incoming.type == "manage-workspace") {
        workspace_manager.manage_workspace(incoming.workspace_id);
        out.push_back(make_message("workspace-managed", incoming.workspace_id, payload_for_workspace_managed(true)));
        out.push_back(make_message(
            "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
        ));
        return out;
    }

    if (incoming.type == "unmanage-workspace") {
        workspace_manager.unmanage_workspace(incoming.workspace_id);
        out.push_back(make_message("workspace-unmanaged", incoming.workspace_id, payload_for_workspace_unmanaged("user-request")));
        out.push_back(make_message(
            "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
        ));
        return out;
    }

    if (incoming.type == "request-state") {
        out.push_back(make_message(
            "state-dump", incoming.workspace_id, serialize_state_dump_payload(workspace_manager.build_state_dump(incoming.workspace_id))
        ));
        return out;
    }

    return out;
}

IpcServer::IpcServer(WorkspaceManager* workspace_manager)
    : workspace_manager_(workspace_manager) {}

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start() {
    if (workspace_manager_ == nullptr) {
        std::cerr << "[hyprmacs] ipc server start failed: missing workspace manager\n";
        return false;
    }

    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return true;
    }

    socket_path_ = default_ipc_socket_path();
    if (!socket_path_.has_value()) {
        std::cerr << "[hyprmacs] ipc server disabled: missing XDG_RUNTIME_DIR\n";
        running_.store(false);
        return false;
    }

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[hyprmacs] ipc server socket() failed: " << std::strerror(errno) << '\n';
        running_.store(false);
        return false;
    }

    ::unlink(socket_path_->c_str());

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (socket_path_->size() >= sizeof(addr.sun_path)) {
        std::cerr << "[hyprmacs] ipc server path too long: " << *socket_path_ << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }
    std::memcpy(addr.sun_path, socket_path_->c_str(), socket_path_->size() + 1);

    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[hyprmacs] ipc server bind failed: " << std::strerror(errno) << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }

    if (::listen(listen_fd_, 4) != 0) {
        std::cerr << "[hyprmacs] ipc server listen failed: " << std::strerror(errno) << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        running_.store(false);
        return false;
    }

    std::cerr << "[hyprmacs] ipc server listening at " << *socket_path_ << '\n';

    accept_thread_ = std::thread([this]() {
        accept_loop();
    });
    return true;
}

void IpcServer::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    if (socket_path_.has_value()) {
        ::unlink(socket_path_->c_str());
    }

    if (workspace_manager_ != nullptr) {
        workspace_manager_->set_controller_connected(false);
    }
}

std::optional<std::string> IpcServer::socket_path() const {
    return socket_path_;
}

void IpcServer::accept_loop() {
    while (running_.load()) {
        int controller_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (controller_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (running_.load()) {
                std::cerr << "[hyprmacs] ipc server accept failed: " << std::strerror(errno) << '\n';
            }
            break;
        }

        workspace_manager_->set_controller_connected(true);
        std::cerr << "[hyprmacs] ipc controller connected\n";
        serve_controller(controller_fd);
        ::shutdown(controller_fd, SHUT_RDWR);
        ::close(controller_fd);
        workspace_manager_->set_controller_connected(false);
        std::cerr << "[hyprmacs] ipc controller disconnected\n";
    }
}

void IpcServer::serve_controller(int controller_fd) {
    std::array<char, 4096> chunk {};
    std::string pending;

    while (running_.load()) {
        const ssize_t read_bytes = ::recv(controller_fd, chunk.data(), chunk.size(), 0);
        if (read_bytes <= 0) {
            if (read_bytes < 0 && errno == EINTR) {
                continue;
            }
            break;
        }

        pending.append(chunk.data(), static_cast<size_t>(read_bytes));

        size_t newline = pending.find('\n');
        while (newline != std::string::npos) {
            const std::string frame = pending.substr(0, newline);
            pending.erase(0, newline + 1);

            if (!frame.empty()) {
                const auto incoming = parse_message(frame);
                if (!incoming.has_value()) {
                    std::cerr << "[hyprmacs] ipc invalid frame dropped\n";
                } else if (incoming->version != 1) {
                    std::cerr << "[hyprmacs] ipc unsupported version: " << incoming->version << '\n';
                } else {
                    std::cerr << "[hyprmacs] ipc recv type=" << incoming->type << " workspace=" << incoming->workspace_id << '\n';
                    const auto responses = route_command_for_tests(*incoming, *workspace_manager_);
                    for (const auto& response : responses) {
                        send_message(controller_fd, response);
                    }
                }
            }

            newline = pending.find('\n');
        }
    }
}

void IpcServer::send_message(int fd, const ProtocolMessage& message) {
    const std::string encoded = serialize_message(message) + "\n";
    const ssize_t sent = ::send(fd, encoded.data(), encoded.size(), 0);
    if (sent < 0) {
        std::cerr << "[hyprmacs] ipc send failed for type=" << message.type << ": " << std::strerror(errno) << '\n';
        return;
    }

    std::cerr << "[hyprmacs] ipc send type=" << message.type << " workspace=" << message.workspace_id << '\n';
}

}  // namespace hyprmacs
