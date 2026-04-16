#pragma once

#include <functional>
#include <string>
#include <unordered_map>

namespace hyprmacs {

class LayoutApplier {
  public:
    using CommandExecutor = std::function<int(const std::string&)>;

    explicit LayoutApplier(CommandExecutor executor);

    bool hide_client(const std::string& client_id, const std::string& workspace_id);
    bool show_client(const std::string& client_id);
    bool is_hidden(const std::string& client_id) const;

  private:
    static std::string normalize_client_id(const std::string& client_id);

    CommandExecutor executor_;
    std::unordered_map<std::string, std::string> hidden_workspace_by_client_;
};

}  // namespace hyprmacs
