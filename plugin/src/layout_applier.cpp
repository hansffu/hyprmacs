#include "hyprmacs/layout_applier.hpp"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace hyprmacs {
namespace {

std::string trim_ascii_ws(std::string_view input) {
    size_t start = 0;
    while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }

    size_t end = input.size();
    while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }

    return std::string(input.substr(start, end - start));
}

void append_layout_debug_log(const std::string& message) {
    const char* override_path = std::getenv("HYPRMACS_LAYOUT_LOG_FILE");
    const std::string path = (override_path != nullptr && *override_path != '\0') ? override_path : "/tmp/hyprmacs-layout.log";

    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};
    localtime_r(&t, &tm);
    char ts[32] {};
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);

    out << "[" << ts << "] " << message << '\n';
}

}  // namespace

LayoutApplier::LayoutApplier(CommandExecutor executor)
    : executor_(std::move(executor)) {}

bool LayoutApplier::hide_client(const std::string& client_id, const std::string& workspace_id) {
    const std::string normalized_client_id = normalize_client_id(client_id);
    if (hidden_workspace_by_client_.find(normalized_client_id) != hidden_workspace_by_client_.end()) {
        append_layout_debug_log("hide skipped (already hidden) client=" + normalized_client_id);
        return true;
    }

    const std::string command = "dispatch movetoworkspacesilent special:hyprmacs-hidden,address:" + normalized_client_id;
    const int rc = executor_(command);
    append_layout_debug_log(
        "hide command rc=" + std::to_string(rc) + " client=" + normalized_client_id + " command=" + command
    );
    if (rc != 0) {
        return false;
    }

    hidden_workspace_by_client_[normalized_client_id] = workspace_id;
    append_layout_debug_log("hide stored mapping client=" + normalized_client_id + " workspace=" + workspace_id);
    return true;
}

bool LayoutApplier::show_client(const std::string& client_id) {
    const std::string normalized_client_id = normalize_client_id(client_id);
    const auto it = hidden_workspace_by_client_.find(normalized_client_id);
    if (it == hidden_workspace_by_client_.end()) {
        append_layout_debug_log("show skipped (already visible) client=" + normalized_client_id);
        return true;
    }

    const std::string command = "dispatch movetoworkspacesilent " + it->second + ",address:" + normalized_client_id;
    const int rc = executor_(command);
    append_layout_debug_log(
        "show command rc=" + std::to_string(rc) + " client=" + normalized_client_id + " command=" + command
    );
    if (rc != 0) {
        return false;
    }

    hidden_workspace_by_client_.erase(it);
    append_layout_debug_log("show cleared mapping client=" + normalized_client_id);
    return true;
}

bool LayoutApplier::is_hidden(const std::string& client_id) const {
    return hidden_workspace_by_client_.find(normalize_client_id(client_id)) != hidden_workspace_by_client_.end();
}

bool LayoutApplier::rectangles_overlap(const LayoutRectangle& lhs, const LayoutRectangle& rhs) {
    const int lhs_right = lhs.x + lhs.width;
    const int lhs_bottom = lhs.y + lhs.height;
    const int rhs_right = rhs.x + rhs.width;
    const int rhs_bottom = rhs.y + rhs.height;

    const bool separated = lhs_right <= rhs.x || rhs_right <= lhs.x || lhs_bottom <= rhs.y || rhs_bottom <= lhs.y;
    return !separated;
}

bool LayoutApplier::validate_non_overlapping(const std::vector<LayoutRectangle>& rectangles, std::string* error_out) {
    for (size_t i = 0; i < rectangles.size(); ++i) {
        const auto& lhs = rectangles[i];
        if (lhs.width <= 0 || lhs.height <= 0) {
            if (error_out != nullptr) {
                *error_out = "rectangle has non-positive size for client " + lhs.client_id;
            }
            return false;
        }

        for (size_t j = i + 1; j < rectangles.size(); ++j) {
            const auto& rhs = rectangles[j];
            if (rectangles_overlap(lhs, rhs)) {
                if (error_out != nullptr) {
                    *error_out = "overlapping rectangles for clients " + lhs.client_id + " and " + rhs.client_id;
                }
                return false;
            }
        }
    }

    return true;
}

bool LayoutApplier::move_resize_client(const LayoutRectangle& rectangle) {
    const std::string normalized_client_id = normalize_client_id(rectangle.client_id);
    if (!ensure_positioning_mode(normalized_client_id)) {
        return false;
    }

    std::ostringstream move;
    move << "dispatch movewindowpixel exact " << rectangle.x << ' ' << rectangle.y << ",address:" << normalized_client_id;
    const int move_rc = executor_(move.str());
    append_layout_debug_log(
        "move command rc=" + std::to_string(move_rc) + " client=" + normalized_client_id + " command=" + move.str()
    );
    if (move_rc != 0) {
        return false;
    }

    std::ostringstream resize;
    resize << "dispatch resizewindowpixel exact " << rectangle.width << ' ' << rectangle.height << ",address:"
           << normalized_client_id;
    const int resize_rc = executor_(resize.str());
    append_layout_debug_log(
        "resize command rc=" + std::to_string(resize_rc) + " client=" + normalized_client_id + " command="
        + resize.str()
    );
    if (resize_rc != 0) {
        return false;
    }

    // Re-assert top-left anchor after resize. Some Hyprland resize paths can shift
    // floating windows vertically, which causes minibuffer-driven geometry drift.
    const int post_move_rc = executor_(move.str());
    append_layout_debug_log(
        "move-post-resize command rc=" + std::to_string(post_move_rc) + " client=" + normalized_client_id
        + " command=" + move.str()
    );
    return post_move_rc == 0;
}

bool LayoutApplier::ensure_positioning_mode(const std::string& normalized_client_id) {
    if (positioning_mode_clients_.find(normalized_client_id) != positioning_mode_clients_.end()) {
        return true;
    }

    // Task 8 prototype path: switch managed clients into geometry-addressable mode
    // before pixel moves/resizes. This is refined in later tasks.
    const std::string command = "dispatch togglefloating address:" + normalized_client_id;
    const int rc = executor_(command);
    append_layout_debug_log(
        "positioning-mode command rc=" + std::to_string(rc) + " client=" + normalized_client_id + " command=" + command
    );
    if (rc != 0) {
        return false;
    }

    positioning_mode_clients_.insert(normalized_client_id);
    return true;
}

bool LayoutApplier::apply_snapshot(const WorkspaceId& workspace_id, const std::vector<LayoutRectangle>& visible_rectangles,
                                   const std::vector<ClientId>& hidden_clients, const std::vector<ClientId>& stacking_order,
                                   std::string* error_out) {
    std::string overlap_error;
    if (!validate_non_overlapping(visible_rectangles, &overlap_error)) {
        if (error_out != nullptr) {
            *error_out = overlap_error;
        }
        append_layout_debug_log("apply_snapshot rejected: " + overlap_error);
        return false;
    }

    std::unordered_set<std::string> visible_client_ids;
    for (const auto& rectangle : visible_rectangles) {
        visible_client_ids.insert(normalize_client_id(rectangle.client_id));
    }

    for (const auto& client_id : hidden_clients) {
        const std::string normalized_client_id = normalize_client_id(client_id);
        if (visible_client_ids.find(normalized_client_id) != visible_client_ids.end()) {
            continue;
        }
        if (!hide_client(normalized_client_id, workspace_id)) {
            if (error_out != nullptr) {
                *error_out = "failed to hide client " + normalized_client_id;
            }
            return false;
        }
    }

    for (const auto& rectangle : visible_rectangles) {
        const std::string normalized_client_id = normalize_client_id(rectangle.client_id);
        if (!show_client(normalized_client_id)) {
            if (error_out != nullptr) {
                *error_out = "failed to show client " + normalized_client_id;
            }
            return false;
        }

        if (!move_resize_client(rectangle)) {
            if (error_out != nullptr) {
                *error_out = "failed to move/resize client " + normalized_client_id;
            }
            return false;
        }
    }

    (void)stacking_order;
    return true;
}

std::string LayoutApplier::normalize_client_id(const std::string& client_id) {
    std::string normalized = trim_ascii_ws(client_id);
    constexpr std::string_view prefix = "address:";
    if (normalized.rfind(prefix, 0) == 0) {
        normalized = normalized.substr(prefix.size());
        normalized = trim_ascii_ws(normalized);
    }

    if (normalized.rfind("0x", 0) == 0) {
        return normalized;
    }
    return "0x" + normalized;
}

}  // namespace hyprmacs
