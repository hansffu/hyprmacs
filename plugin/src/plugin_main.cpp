#include <cstdlib>
#include <cstring>
#include <charconv>
#include <cstdint>
#include <cerrno>
#include <iostream>
#include <optional>
#include <string>
#include <array>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>
#include <cctype>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "hyprmacs/focus_controller.hpp"
#include "hyprmacs/ipc_server.hpp"
#include "hyprmacs/layout_applier.hpp"
#include "hyprmacs/dispatchers.hpp"
#include "hyprmacs/workspace_manager.hpp"

#ifdef HYPRMACS_PLUGIN_MAIN_UNIT_TEST
#define HYPRMACS_HAS_REAL_PLUGIN_API 0
#define HYPRMACS_CAN_REGISTER_TILED_ALGO 0
#include <hyprland/src/helpers/math/Math.hpp>
#elif __has_include(<hyprland/src/plugins/PluginAPI.hpp>) && __has_include(<hyprgraphics/color/Color.hpp>)
#define HYPRMACS_HAS_REAL_PLUGIN_API 1
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#if __has_include(<hyprland/src/layout/algorithm/TiledAlgorithm.hpp>) && __has_include(<hyprland/src/layout/space/Space.hpp>) && \
    __has_include(<hyprland/src/layout/target/Target.hpp>)
#define HYPRMACS_CAN_REGISTER_TILED_ALGO 1
#include <hyprland/src/layout/algorithm/TiledAlgorithm.hpp>
#include <hyprland/src/layout/algorithm/Algorithm.hpp>
#include <hyprland/src/layout/space/Space.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#else
#define HYPRMACS_CAN_REGISTER_TILED_ALGO 0
#endif
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

namespace hyprmacs {
std::optional<std::string> build_workspace_recalc_dispatch_command(std::string_view workspace_id);
bool request_workspace_recalc_marshaled(const WorkspaceId& workspace_id, const std::function<int(const std::string&)>& dispatcher);
}

namespace {
constexpr auto kPluginName = "hyprmacs";
constexpr auto kPluginDescription =
    "Bootstrap plugin scaffold for Emacs-directed Hyprland workspaces.";
constexpr auto kPluginAuthor = "Hans Fredrik Furholt";
constexpr auto kPluginVersion = "0.1.0";

std::optional<std::string>& cached_command_socket_path() {
    static std::optional<std::string> path;
    return path;
}

std::optional<std::string> build_command_socket_path(std::string_view runtime_dir, std::string_view instance) {
    if (runtime_dir.empty() || instance.empty()) {
        return std::nullopt;
    }

    std::string socket_path(runtime_dir);
    if (!socket_path.empty() && socket_path.back() != '/') {
        socket_path.push_back('/');
    }
    socket_path += "hypr/";
    socket_path += std::string(instance);
    socket_path += "/.socket.sock";
    return socket_path;
}

std::optional<std::string> detect_command_socket_path_from_env() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    const char* instance = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (runtime_dir == nullptr || instance == nullptr || *runtime_dir == '\0' || *instance == '\0') {
        return std::nullopt;
    }

    return build_command_socket_path(runtime_dir, instance);
}

std::optional<std::string> resolve_command_socket_path() {
    auto& cached = cached_command_socket_path();
    if (cached.has_value()) {
        return cached;
    }

    cached = detect_command_socket_path_from_env();
    return cached;
}

void initialize_command_socket_path_from_env() {
    auto& cached = cached_command_socket_path();
    if (cached.has_value()) {
        return;
    }
    cached = detect_command_socket_path_from_env();
    if (!cached.has_value()) {
        std::cerr << "[hyprmacs] command socket bootstrap failed: missing runtime env at plugin init\n";
        return;
    }
    std::cerr << "[hyprmacs] command socket path initialized: " << *cached << '\n';
}

std::optional<std::string> send_hypr_command_via_socket(const std::string& command) {
    const auto socket_path_opt = resolve_command_socket_path();
    if (!socket_path_opt.has_value()) {
        std::cerr << "[hyprmacs] dispatch failed: missing runtime env\n";
        return std::nullopt;
    }
    const std::string& socket_path = *socket_path_opt;

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "[hyprmacs] dispatch failed: socket() rc=" << fd << '\n';
        return std::nullopt;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        std::cerr << "[hyprmacs] dispatch failed: socket path too long path=" << socket_path << '\n';
        ::close(fd);
        return std::nullopt;
    }
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);

    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[hyprmacs] dispatch failed: connect() path=" << socket_path << " errno=" << errno << '\n';
        ::close(fd);
        return std::nullopt;
    }

    if (::send(fd, command.c_str(), command.size(), 0) < 0) {
        std::cerr << "[hyprmacs] dispatch failed: send() errno=" << errno << '\n';
        ::close(fd);
        return std::nullopt;
    }
    (void)::shutdown(fd, SHUT_WR);

    std::string response;
    std::array<char, 4096> chunk {};
    while (true) {
        const ssize_t read_bytes = ::recv(fd, chunk.data(), chunk.size(), 0);
        if (read_bytes < 0) {
            std::cerr << "[hyprmacs] dispatch failed: recv() errno=" << errno << '\n';
            ::close(fd);
            return std::nullopt;
        }
        if (read_bytes == 0) {
            break;
        }
        response.append(chunk.data(), static_cast<size_t>(read_bytes));
    }
    ::close(fd);
    return response;
}

int dispatch_hypr_command_via_socket(const std::string& command) {
    const auto reply_opt = send_hypr_command_via_socket(command);
    if (!reply_opt.has_value()) {
        return -1;
    }

    const std::string& reply = *reply_opt;
    if (reply.find("ok") == std::string::npos) {
        std::cerr << "[hyprmacs] dispatch non-ok response: " << reply << '\n';
        return 1;
    }
    return 0;
}

std::string trim_ascii_copy(std::string_view text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }

    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return std::string(text.substr(start, end - start));
}

#ifndef HYPRMACS_PLUGIN_MAIN_UNIT_TEST
void request_workspace_recalc(const hyprmacs::WorkspaceId& workspace_id) {
#if HYPRMACS_HAS_REAL_PLUGIN_API
    if (workspace_id.empty() || !g_pCompositor) {
        return;
    }

    auto workspace = g_pCompositor->getWorkspaceByString(workspace_id);
    if (!workspace) {
        WORKSPACEID numeric_workspace_id = WORKSPACE_INVALID;
        const char* begin = workspace_id.data();
        const char* end = begin + workspace_id.size();
        const auto parsed = std::from_chars(begin, end, numeric_workspace_id);
        if (parsed.ec == std::errc {} && parsed.ptr == end) {
            workspace = g_pCompositor->getWorkspaceByID(numeric_workspace_id);
        }
    }

    if (!workspace || !workspace->m_space) {
        return;
    }

    workspace->m_space->recalculate();
#else
    (void)workspace_id;
#endif
}

hyprmacs::WorkspaceManager g_workspace_manager(
    [](const std::string& command) {
        return dispatch_hypr_command_via_socket(command);
    },
    [](const std::string& command) -> std::optional<std::string> {
        return send_hypr_command_via_socket(command);
    }
);
hyprmacs::LayoutApplier g_layout_applier([](const std::string& command) {
    return dispatch_hypr_command_via_socket(command);
});
hyprmacs::FocusController g_focus_controller([](const std::string& command) {
    return dispatch_hypr_command_via_socket(command);
});
hyprmacs::IpcServer g_ipc_server(
    &g_workspace_manager,
    &g_layout_applier,
    &g_focus_controller,
    [](const hyprmacs::WorkspaceId& workspace_id) -> bool {
        return hyprmacs::request_workspace_recalc_marshaled(workspace_id, [](const std::string& command) {
            return dispatch_hypr_command_via_socket(command);
        });
    }
);
#endif
}

namespace hyprmacs {

enum class ManagedTargetVisibilityAction {
    kIgnore,
    kShow,
    kHide,
};

std::optional<std::string> build_workspace_recalc_dispatch_command(std::string_view workspace_id) {
    const std::string trimmed_workspace_id = trim_ascii_copy(workspace_id);
    if (trimmed_workspace_id.empty()) {
        return std::nullopt;
    }

    return "dispatch hyprmacs:request-recalc " + trimmed_workspace_id;
}

bool request_workspace_recalc_marshaled(const WorkspaceId& workspace_id, const std::function<int(const std::string&)>& dispatcher) {
    if (!dispatcher) {
        return false;
    }

    const auto command = build_workspace_recalc_dispatch_command(workspace_id);
    if (!command.has_value()) {
        return false;
    }

    return dispatcher(*command) == 0;
}

namespace {

std::string normalize_client_id_for_recalc(std::string_view client_id) {
    constexpr std::string_view kAddressPrefix = "address:";

    std::string normalized(client_id);
    if (normalized.rfind(kAddressPrefix.data(), 0) == 0) {
        normalized.erase(0, kAddressPrefix.size());
    }

    if (normalized.empty()) {
        return normalized;
    }

    if (normalized.rfind("0x", 0) != 0) {
        normalized = "0x" + normalized;
    }

    return normalized;
}

bool snapshot_client_ids_match(std::string_view lhs, std::string_view rhs) {
    return normalize_client_id_for_recalc(lhs) == normalize_client_id_for_recalc(rhs);
}

bool snapshot_client_id_list_contains(const std::vector<ClientId>& client_ids, std::string_view normalized_target_client_id) {
    return std::find_if(
               client_ids.begin(),
               client_ids.end(),
               [&](const ClientId& client_id) { return snapshot_client_ids_match(client_id, normalized_target_client_id); }
           ) != client_ids.end();
}

}  // namespace

ManagedTargetVisibilityAction compute_managed_target_visibility_action_for_recalc(const ManagedWorkspaceLayoutSnapshot& snapshot,
                                                                                  std::string_view target_workspace_id,
                                                                                  std::string_view target_client_id,
                                                                                  bool target_floating,
                                                                                  bool target_is_hidden) {
    if (snapshot.workspace_id.empty() || target_workspace_id.empty()) {
        return ManagedTargetVisibilityAction::kIgnore;
    }

    const std::string normalized_target_client_id = normalize_client_id_for_recalc(target_client_id);
    if (normalized_target_client_id.empty()) {
        return ManagedTargetVisibilityAction::kIgnore;
    }

    if (snapshot.managing_emacs_client_id.has_value() &&
        snapshot_client_ids_match(*snapshot.managing_emacs_client_id, normalized_target_client_id)) {
        return ManagedTargetVisibilityAction::kIgnore;
    }

    if (target_floating) {
        return ManagedTargetVisibilityAction::kIgnore;
    }

    const bool target_is_visible = snapshot_client_id_list_contains(snapshot.visible_client_ids, normalized_target_client_id);
    const bool target_is_hidden_in_snapshot = snapshot_client_id_list_contains(snapshot.hidden_client_ids, normalized_target_client_id);

    if (!target_is_visible && !target_is_hidden_in_snapshot) {
        return ManagedTargetVisibilityAction::kIgnore;
    }

    if (!target_is_hidden && snapshot.workspace_id != target_workspace_id) {
        return ManagedTargetVisibilityAction::kIgnore;
    }

    if (target_is_visible) {
        return ManagedTargetVisibilityAction::kShow;
    }

    if (target_is_hidden_in_snapshot) {
        return ManagedTargetVisibilityAction::kHide;
    }

    return ManagedTargetVisibilityAction::kIgnore;
}

std::optional<CBox> compute_managed_target_box_for_recalc(const ManagedWorkspaceLayoutSnapshot& snapshot,
                                                          std::string_view target_workspace_id,
                                                          std::string_view target_client_id,
                                                          bool target_floating,
                                                          const CBox& work_area) {
    if (snapshot.workspace_id.empty() || target_workspace_id.empty() || snapshot.workspace_id != target_workspace_id) {
        return std::nullopt;
    }

    const std::string normalized_target_client_id = normalize_client_id_for_recalc(target_client_id);
    if (normalized_target_client_id.empty()) {
        return std::nullopt;
    }

    if (snapshot.managing_emacs_client_id.has_value() &&
        snapshot_client_ids_match(*snapshot.managing_emacs_client_id, normalized_target_client_id)) {
        return work_area;
    }

    if (target_floating) {
        return std::nullopt;
    }

    const auto visible_it = std::find_if(
        snapshot.visible_client_ids.begin(),
        snapshot.visible_client_ids.end(),
        [&](const ClientId& client_id) { return snapshot_client_ids_match(client_id, normalized_target_client_id); }
    );
    if (visible_it == snapshot.visible_client_ids.end()) {
        return std::nullopt;
    }

    const auto rectangle_it = std::find_if(
        snapshot.rectangles_by_client_id.begin(),
        snapshot.rectangles_by_client_id.end(),
        [&](const auto& entry) { return snapshot_client_ids_match(entry.first, normalized_target_client_id); }
    );
    if (rectangle_it == snapshot.rectangles_by_client_id.end()) {
        return std::nullopt;
    }

    const auto& rectangle = rectangle_it->second;
    return CBox {
        static_cast<double>(rectangle.x),
        static_cast<double>(rectangle.y),
        static_cast<double>(rectangle.width),
        static_cast<double>(rectangle.height),
    };
}

}  // namespace hyprmacs

#ifndef HYPRMACS_PLUGIN_MAIN_UNIT_TEST
#if HYPRMACS_HAS_REAL_PLUGIN_API
inline HANDLE PHANDLE = nullptr;
#if HYPRMACS_CAN_REGISTER_TILED_ALGO

namespace {

std::optional<std::string> target_client_id_for_recalc(const SP<Layout::ITarget>& target) {
    if (!target) {
        return std::nullopt;
    }

    const auto window = target->window();
    if (!window) {
        return std::nullopt;
    }

    std::ostringstream out;
    out << "0x" << std::hex << std::nouppercase << reinterpret_cast<std::uintptr_t>(window.get());
    return out.str();
}

std::optional<std::string> target_workspace_id_for_recalc(const SP<Layout::ITarget>& target) {
    if (!target) {
        return std::nullopt;
    }

    const auto workspace = target->workspace();
    if (!valid(workspace)) {
        return std::nullopt;
    }

    return std::to_string(workspace->m_id);
}

}  // namespace

class CHyprmacsAlgorithm final : public Layout::ITiledAlgorithm {
  public:
    void newTarget(SP<Layout::ITarget> target) override {
        if (!target) {
            return;
        }
        const auto it = std::ranges::find_if(m_targets_, [&target](const auto& weak) { return weak.lock() == target; });
        if (it == m_targets_.end()) {
            m_targets_.push_back(target);
        }
        recalculate();
    }

    void movedTarget(SP<Layout::ITarget> target, std::optional<Vector2D> focalPoint = std::nullopt) override {
        (void)focalPoint;
        newTarget(target);
    }

    void removeTarget(SP<Layout::ITarget> target) override {
        std::erase_if(m_targets_, [&target](const auto& weak) { return !weak.lock() || weak.lock() == target; });
        recalculate();
    }

    void resizeTarget(const Vector2D& delta, SP<Layout::ITarget> target, Layout::eRectCorner corner = Layout::CORNER_NONE) override {
        (void)delta;
        (void)target;
        (void)corner;
    }

    void recalculate() override {
        compact();
        const auto managed_workspace_id = g_workspace_manager.managed_workspace();
        if (!managed_workspace_id.has_value()) {
            return;
        }

        const auto snapshot = g_workspace_manager.managed_layout_snapshot(*managed_workspace_id);
        if (!snapshot.has_value()) {
            return;
        }

        struct TargetRecalcContext {
            SP<Layout::ITarget> target;
            std::string workspace_id;
            std::string client_id;
            CBox work_area;
            bool floating = false;
        };

        std::vector<TargetRecalcContext> target_contexts;
        target_contexts.reserve(m_targets_.size());
        for (const auto& weak_target : m_targets_) {
            const auto target = weak_target.lock();
            if (!target) {
                continue;
            }

            const auto target_workspace_id = target_workspace_id_for_recalc(target);
            const auto target_client_id = target_client_id_for_recalc(target);
            const auto space = target->space();
            if (!target_workspace_id.has_value() || !target_client_id.has_value() || !space) {
                continue;
            }

            target_contexts.push_back(TargetRecalcContext {
                .target = target,
                .workspace_id = *target_workspace_id,
                .client_id = *target_client_id,
                .work_area = space->workArea(),
                .floating = target->floating(),
            });
        }

        const auto find_target_context = [&](std::string_view snapshot_client_id) -> const TargetRecalcContext* {
            const auto it = std::find_if(target_contexts.begin(), target_contexts.end(), [&](const TargetRecalcContext& context) {
                return hyprmacs::snapshot_client_ids_match(snapshot_client_id, context.client_id);
            });
            if (it == target_contexts.end()) {
                return nullptr;
            }
            return &(*it);
        };

        // Process targets in explicit class order to preserve layering semantics:
        // 1) managing Emacs baseline, 2) managed non-floating targets (stacking order),
        // 3) hidden managed targets.
        if (snapshot->managing_emacs_client_id.has_value()) {
            const auto* emacs_context = find_target_context(*snapshot->managing_emacs_client_id);
            if (emacs_context != nullptr) {
                const auto emacs_box = hyprmacs::compute_managed_target_box_for_recalc(
                    *snapshot,
                    emacs_context->workspace_id,
                    emacs_context->client_id,
                    emacs_context->floating,
                    emacs_context->work_area
                );
                if (emacs_box.has_value()) {
                    emacs_context->target->setPositionGlobal(*emacs_box);
                }
            }
        }

        for (const auto& visible_client_id : snapshot->stacking_order) {
            const auto* target_context = find_target_context(visible_client_id);
            if (target_context == nullptr) {
                continue;
            }

            const std::string normalized_target_client_id = hyprmacs::normalize_client_id_for_recalc(target_context->client_id);
            const bool target_is_hidden = g_layout_applier.is_hidden(normalized_target_client_id);
            const auto visibility_action = hyprmacs::compute_managed_target_visibility_action_for_recalc(
                *snapshot,
                target_context->workspace_id,
                target_context->client_id,
                target_context->floating,
                target_is_hidden
            );
            if (visibility_action == hyprmacs::ManagedTargetVisibilityAction::kHide) {
                if (!target_is_hidden) {
                    (void)g_layout_applier.hide_client(normalized_target_client_id, target_context->workspace_id);
                }
                continue;
            }

            if (visibility_action == hyprmacs::ManagedTargetVisibilityAction::kShow && target_is_hidden) {
                (void)g_layout_applier.show_client(normalized_target_client_id);
            }

            const std::string workspace_id_for_geometry =
                (visibility_action == hyprmacs::ManagedTargetVisibilityAction::kShow && target_is_hidden)
                ? snapshot->workspace_id
                : target_context->workspace_id;
            const auto target_box = hyprmacs::compute_managed_target_box_for_recalc(
                *snapshot,
                workspace_id_for_geometry,
                target_context->client_id,
                target_context->floating,
                target_context->work_area
            );
            if (!target_box.has_value()) {
                continue;
            }

            target_context->target->setPositionGlobal(*target_box);
        }

        for (const auto& hidden_client_id : snapshot->hidden_client_ids) {
            const auto* target_context = find_target_context(hidden_client_id);
            if (target_context == nullptr) {
                continue;
            }

            const std::string normalized_target_client_id = hyprmacs::normalize_client_id_for_recalc(target_context->client_id);
            const bool target_is_hidden = g_layout_applier.is_hidden(normalized_target_client_id);
            const auto visibility_action = hyprmacs::compute_managed_target_visibility_action_for_recalc(
                *snapshot,
                target_context->workspace_id,
                target_context->client_id,
                target_context->floating,
                target_is_hidden
            );
            if (visibility_action == hyprmacs::ManagedTargetVisibilityAction::kHide && !target_is_hidden) {
                (void)g_layout_applier.hide_client(normalized_target_client_id, target_context->workspace_id);
            }
        }
    }

    SP<Layout::ITarget> getNextCandidate(SP<Layout::ITarget> old) override {
        compact();
        if (m_targets_.empty()) {
            return nullptr;
        }

        size_t old_index = 0;
        bool found = false;
        for (size_t i = 0; i < m_targets_.size(); ++i) {
            if (m_targets_[i].lock() == old) {
                old_index = i;
                found = true;
                break;
            }
        }

        if (!found) {
            return m_targets_.front().lock();
        }

        const size_t next = (old_index + 1) % m_targets_.size();
        return m_targets_[next].lock();
    }

    std::expected<void, std::string> layoutMsg(const std::string_view& sv) override {
        (void)sv;
        return {};
    }

    std::optional<Vector2D> predictSizeForNewTarget() override {
        return std::nullopt;
    }

    void swapTargets(SP<Layout::ITarget> a, SP<Layout::ITarget> b) override {
        if (!a || !b) {
            return;
        }
        size_t ia = m_targets_.size();
        size_t ib = m_targets_.size();
        for (size_t i = 0; i < m_targets_.size(); ++i) {
            const auto current = m_targets_[i].lock();
            if (current == a) {
                ia = i;
            } else if (current == b) {
                ib = i;
            }
        }
        if (ia < m_targets_.size() && ib < m_targets_.size()) {
            std::swap(m_targets_[ia], m_targets_[ib]);
        }
        recalculate();
    }

    void moveTargetInDirection(SP<Layout::ITarget> target, Math::eDirection direction, bool silent) override {
        (void)target;
        (void)direction;
        (void)silent;
    }

  private:
    void compact() {
        std::erase_if(m_targets_, [](const auto& weak) { return !weak.lock(); });
    }

    std::vector<WP<Layout::ITarget>> m_targets_;
};

bool g_hyprmacs_layout_registered = false;
bool g_set_emacs_control_dispatcher_registered = false;
bool g_request_recalc_dispatcher_registered = false;

std::function<UP<Layout::ITiledAlgorithm>()> make_hyprmacs_tiled_factory() {
    return [] {
        return makeUnique<CHyprmacsAlgorithm>();
    };
}
#endif

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;
    initialize_command_socket_path_from_env();

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

#if HYPRMACS_CAN_REGISTER_TILED_ALGO
    g_hyprmacs_layout_registered = HyprlandAPI::addTiledAlgo(
        PHANDLE,
        "hyprmacs",
        &typeid(CHyprmacsAlgorithm),
        make_hyprmacs_tiled_factory()
    );
    if (!g_hyprmacs_layout_registered) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[hyprmacs] failed to register hyprmacs tiled layout",
            CHyprColor {1.0, 0.2, 0.2, 1.0},
            5000
        );
    }
#endif

    g_set_emacs_control_dispatcher_registered = HyprlandAPI::addDispatcherV2(
        PHANDLE,
        "hyprmacs:set-emacs-control-mode",
        [](std::string arg) -> SDispatchResult {
            const auto outcome = hyprmacs::dispatch_set_emacs_control_mode(arg, g_workspace_manager);
            if (outcome.success && outcome.focus_client_id.has_value()) {
                const auto focus_target = *outcome.focus_client_id;
                std::thread([focus_target]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    (void)g_focus_controller.focus_client(focus_target);
                }).detach();
            }
            if (outcome.workspace_id.has_value()) {
                g_ipc_server.publish_state_dump_for_workspace(*outcome.workspace_id);
            }
            return {
                .passEvent = false,
                .success = outcome.success,
                .error = outcome.error,
            };
        }
    );
    if (!g_set_emacs_control_dispatcher_registered) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[hyprmacs] failed to register dispatcher hyprmacs:set-emacs-control-mode",
            CHyprColor {1.0, 0.2, 0.2, 1.0},
            5000
        );
    }

    g_request_recalc_dispatcher_registered = HyprlandAPI::addDispatcherV2(
        PHANDLE,
        "hyprmacs:request-recalc",
        [](std::string arg) -> SDispatchResult {
            const std::string workspace_id = trim_ascii_copy(arg);
            if (workspace_id.empty()) {
                return {
                    .passEvent = false,
                    .success = false,
                    .error = "workspace id required",
                };
            }

            request_workspace_recalc(workspace_id);
            return {
                .passEvent = false,
                .success = true,
                .error = "",
            };
        }
    );
    if (!g_request_recalc_dispatcher_registered) {
        HyprlandAPI::addNotification(
            PHANDLE,
            "[hyprmacs] failed to register dispatcher hyprmacs:request-recalc",
            CHyprColor {1.0, 0.2, 0.2, 1.0},
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
#if HYPRMACS_HAS_REAL_PLUGIN_API
    if (g_request_recalc_dispatcher_registered) {
        (void)HyprlandAPI::removeDispatcher(PHANDLE, "hyprmacs:request-recalc");
        g_request_recalc_dispatcher_registered = false;
    }
    if (g_set_emacs_control_dispatcher_registered) {
        (void)HyprlandAPI::removeDispatcher(PHANDLE, "hyprmacs:set-emacs-control-mode");
        g_set_emacs_control_dispatcher_registered = false;
    }
#endif
#if HYPRMACS_CAN_REGISTER_TILED_ALGO
    if (g_hyprmacs_layout_registered) {
        (void)HyprlandAPI::removeAlgo(PHANDLE, "hyprmacs");
        g_hyprmacs_layout_registered = false;
    }
#endif
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
    initialize_command_socket_path_from_env();
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
#endif
