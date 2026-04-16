#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "hyprmacs/ipc_server.hpp"
#include "hyprmacs/layout_applier.hpp"
#include "hyprmacs/workspace_manager.hpp"

#if __has_include(<hyprland/src/plugins/PluginAPI.hpp>)
#define HYPRMACS_HAS_REAL_PLUGIN_API 1
#include <hyprland/src/plugins/PluginAPI.hpp>
#else
#define HYPRMACS_HAS_REAL_PLUGIN_API 0

#if defined(__GNUC__) || defined(__clang__)
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

#define APICALL extern "C"

using HANDLE = void*;

struct PLUGIN_DESCRIPTION_INFO {
    std::string name;
    std::string description;
    std::string author;
    std::string version;
};

static constexpr const char* HYPRLAND_API_VERSION = "bootstrap-fallback";
#endif

namespace {
constexpr auto kPluginName = "hyprmacs";
constexpr auto kPluginDescription =
    "Bootstrap plugin scaffold for Emacs-directed Hyprland workspaces.";
constexpr auto kPluginAuthor = "Hans Fredrik Furholt";
constexpr auto kPluginVersion = "0.1.0";

int dispatch_via_hypr_socket(const std::string& dispatch_arg) {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    const char* instance = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (runtime_dir == nullptr || instance == nullptr || *runtime_dir == '\0' || *instance == '\0') {
        std::cerr << "[hyprmacs] dispatch failed: missing runtime env\n";
        return -1;
    }

    std::string socket_path = runtime_dir;
    if (!socket_path.empty() && socket_path.back() != '/') {
        socket_path.push_back('/');
    }
    socket_path += "hypr/";
    socket_path += instance;
    socket_path += "/.socket.sock";

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "[hyprmacs] dispatch failed: socket() rc=" << fd << '\n';
        return -1;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        std::cerr << "[hyprmacs] dispatch failed: socket path too long path=" << socket_path << '\n';
        ::close(fd);
        return -1;
    }
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);

    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[hyprmacs] dispatch failed: connect() path=" << socket_path << " errno=" << errno << '\n';
        ::close(fd);
        return -1;
    }

    std::string command = "dispatch movetoworkspacesilent " + dispatch_arg;
    command.push_back('\n');
    if (::send(fd, command.c_str(), command.size(), 0) < 0) {
        std::cerr << "[hyprmacs] dispatch failed: send() errno=" << errno << '\n';
        ::close(fd);
        return -1;
    }
    (void)::shutdown(fd, SHUT_WR);

    char response[256] {};
    const ssize_t read_bytes = ::recv(fd, response, sizeof(response) - 1, 0);
    ::close(fd);
    if (read_bytes < 0) {
        std::cerr << "[hyprmacs] dispatch failed: recv() errno=" << errno << '\n';
        return -1;
    }

    response[read_bytes] = '\0';
    const std::string reply(response);
    if (reply.find("ok") == std::string::npos) {
        std::cerr << "[hyprmacs] dispatch non-ok response: " << reply << '\n';
        return 1;
    }
    return 0;
}

hyprmacs::WorkspaceManager g_workspace_manager;
hyprmacs::LayoutApplier g_layout_applier([](const std::string& dispatch_arg) {
    return dispatch_via_hypr_socket(dispatch_arg);
});
hyprmacs::IpcServer g_ipc_server(&g_workspace_manager, &g_layout_applier);
}

#if HYPRMACS_HAS_REAL_PLUGIN_API
inline HANDLE PHANDLE = nullptr;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string compositor_hash = __hyprland_api_get_hash();
    const std::string client_hash = __hyprland_api_get_client_hash();

    if (compositor_hash != client_hash) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[hyprmacs] API hash mismatch detected; continuing in bootstrap mode.",
            CHyprColor {1.0, 0.7, 0.2, 1.0},
            5000
        );
    }

    g_workspace_manager.start_event_tap();
    g_ipc_server.start();

    HyprlandAPI::addNotification(
        PHANDLE,
        "[hyprmacs] event tap + ipc enabled (Task 6)",
        CHyprColor {0.2, 1.0, 0.4, 1.0},
        4000
    );

    return {kPluginName, kPluginDescription, kPluginAuthor, kPluginVersion};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_ipc_server.stop();
    g_workspace_manager.stop_event_tap();
}
#else
// Fallback bootstrap path for agent sandboxes that do not expose Hyprland headers.
// The real nested-Hyprland validation must still be done in the user's Nix env.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE) {
    g_workspace_manager.start_event_tap();
    g_ipc_server.start();
    return {
        kPluginName,
        std::string {kPluginDescription} + " Built without Hyprland headers.",
        kPluginAuthor,
        kPluginVersion,
    };
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_ipc_server.stop();
    g_workspace_manager.stop_event_tap();
}
#endif
