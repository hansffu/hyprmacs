#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "hyprmacs/plugin_state.hpp"

namespace hyprmacs {

struct LayoutRectangle {
    ClientId client_id;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class LayoutApplier {
  public:
    using CommandExecutor = std::function<int(const std::string&)>;

    explicit LayoutApplier(CommandExecutor executor);

    bool hide_client(const std::string& client_id, const std::string& workspace_id);
    bool hide_client_force(const std::string& client_id, const std::string& workspace_id);
    bool show_client(const std::string& client_id);
    bool is_hidden(const std::string& client_id) const;
    bool ensure_client_floating(const std::string& client_id);
    bool ensure_client_tiled(const std::string& client_id);
    bool apply_floating_geometry(const LayoutRectangle& rectangle);
    bool lower_client_zorder(const std::string& client_id);
    bool raise_client_zorder(const std::string& client_id);
    bool restore_workspace_to_native(const WorkspaceId& workspace_id, const std::vector<ClientId>& managed_clients);
    bool apply_snapshot(const WorkspaceId& workspace_id, const std::vector<LayoutRectangle>& visible_rectangles,
                        const std::vector<ClientId>& hidden_clients, const std::vector<ClientId>& stacking_order,
                        std::string* error_out = nullptr);

    static bool validate_non_overlapping(const std::vector<LayoutRectangle>& rectangles, std::string* error_out = nullptr);

  private:
    static std::string normalize_client_id(const std::string& client_id);
    static bool rectangles_overlap(const LayoutRectangle& lhs, const LayoutRectangle& rhs);

    CommandExecutor executor_;
    std::unordered_map<std::string, std::string> hidden_workspace_by_client_;
};

}  // namespace hyprmacs
