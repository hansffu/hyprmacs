#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hyprmacs {

struct ClientRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ClientRecord {
    std::string client_id;
    std::string workspace_id;
    std::string title;
    std::string app_id;

    bool visible = false;
    bool selected = false;
    bool floating = false;
    bool eligible = false;
    bool managed = false;

    std::optional<ClientRect> rectangle;
    std::optional<std::string> buffer_id;
};

struct RegistrySnapshot {
    std::vector<ClientRecord> clients;
    std::optional<std::string> selected_client;
};

class ClientRegistry {
  public:
    void upsert_open(const std::string& client_id, const std::string& workspace_id, const std::string& app_id,
                     const std::string& title);
    void erase(const std::string& client_id);

    void update_workspace(const std::string& client_id, const std::string& workspace_id);
    void update_title(const std::string& client_id, const std::string& title);
    void set_focus(const std::string& client_id);
    void set_floating(const std::string& client_id, bool floating);

    const ClientRecord* find(const std::string& client_id) const;
    RegistrySnapshot snapshot() const;

  private:
    std::unordered_map<std::string, ClientRecord> clients_;
    std::optional<std::string> selected_client_;
};

}  // namespace hyprmacs
