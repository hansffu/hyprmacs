#include "hyprmacs/client_registry.hpp"

#include <algorithm>

#include "hyprmacs/client_classifier.hpp"

namespace hyprmacs {

void ClientRegistry::upsert_open(const std::string& client_id, const std::string& workspace_id, const std::string& app_id,
                                 const std::string& title) {
    auto& client = clients_[client_id];
    client.client_id = client_id;
    client.workspace_id = workspace_id;
    client.app_id = app_id;
    client.title = title;
    client.eligible = is_client_eligible(client);
}

void ClientRegistry::erase(const std::string& client_id) {
    clients_.erase(client_id);
    if (selected_client_.has_value() && *selected_client_ == client_id) {
        selected_client_ = std::nullopt;
    }
}

void ClientRegistry::update_workspace(const std::string& client_id, const std::string& workspace_id) {
    const auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return;
    }
    it->second.workspace_id = workspace_id;
}

void ClientRegistry::update_title(const std::string& client_id, const std::string& title) {
    const auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return;
    }
    it->second.title = title;
    it->second.eligible = is_client_eligible(it->second);
}

void ClientRegistry::set_focus(const std::string& client_id) {
    if (clients_.find(client_id) == clients_.end()) {
        return;
    }

    selected_client_ = client_id;
    for (auto& [id, client] : clients_) {
        client.selected = (id == client_id);
    }
}

void ClientRegistry::set_floating(const std::string& client_id, bool floating) {
    const auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return;
    }
    it->second.floating = floating;
    it->second.eligible = is_client_eligible(it->second);
}

const ClientRecord* ClientRegistry::find(const std::string& client_id) const {
    const auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return nullptr;
    }
    return &it->second;
}

RegistrySnapshot ClientRegistry::snapshot() const {
    RegistrySnapshot out;
    out.selected_client = selected_client_;
    out.clients.reserve(clients_.size());

    for (const auto& [_, client] : clients_) {
        out.clients.push_back(client);
    }

    std::sort(out.clients.begin(), out.clients.end(), [](const ClientRecord& a, const ClientRecord& b) {
        return a.client_id < b.client_id;
    });

    return out;
}

}  // namespace hyprmacs
