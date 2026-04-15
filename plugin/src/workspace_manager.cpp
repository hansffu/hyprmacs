#include "hyprmacs/workspace_manager.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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
    if (event_name == "openwindow" || event_name == "closewindow" || event_name == "movewindow" ||
        event_name == "activewindow" || event_name == "windowtitle" || event_name == "changefloatingmode" ||
        event_name == "changefloatingmodev2") {
        return true;
    }

    return event_name.find("floating") != std::string_view::npos;
}

WorkspaceManager::WorkspaceManager() = default;

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

    {
        std::scoped_lock lock(mutex_);
        event_counts_[frame->name] += 1;
    }

    std::cerr << "[hyprmacs] event: " << frame->name;
    if (!frame->payload.empty()) {
        std::cerr << " payload=" << frame->payload;
    }
    std::cerr << '\n';
}

bool WorkspaceManager::should_track(const std::string& event_name) const {
    return is_tracked_event_name(event_name);
}

}  // namespace hyprmacs
