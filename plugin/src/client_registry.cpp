#include "hyprmacs/client_registry.hpp"

#include <algorithm>

#include "hyprmacs/client_classifier.hpp"

namespace hyprmacs {
namespace {

std::string normalize_client_id(std::string_view client_id) {
    if (client_id.rfind("address:", 0) == 0) {
        client_id = client_id.substr(8);
    }
    if (client_id.rfind("0x", 0) == 0) {
        return std::string(client_id);
    }
    return "0x" + std::string(client_id);
}

}  // namespace

void ClientRegistry::upsert_open(const std::string& client_id, const std::string& workspace_id, const std::string& app_id,
                                 const std::string& title) {
    const std::string normalized_id = normalize_client_id(client_id);
    auto& client = clients_[normalized_id];
    client.client_id = normalized_id;
    client.workspace_id = workspace_id;
    client.app_id = app_id;
    client.title = title;
    client.eligible = is_client_eligible(client);
}

void ClientRegistry::erase(const std::string& client_id) {
    const std::string normalized_id = normalize_client_id(client_id);
    clients_.erase(normalized_id);
    if (selected_client_.has_value() && *selected_client_ == normalized_id) {
        selected_client_ = std::nullopt;
    }
}

void ClientRegistry::update_workspace(const std::string& client_id, const std::string& workspace_id) {
    const auto it = clients_.find(normalize_client_id(client_id));
    if (it == clients_.end()) {
        return;
    }
    it->second.workspace_id = workspace_id;
}

void ClientRegistry::update_title(const std::string& client_id, const std::string& title) {
    const auto it = clients_.find(normalize_client_id(client_id));
    if (it == clients_.end()) {
        return;
    }
    it->second.title = title;
    it->second.eligible = is_client_eligible(it->second);
}

void ClientRegistry::set_focus(const std::string& client_id) {
    const std::string normalized_id = normalize_client_id(client_id);
    if (clients_.find(normalized_id) == clients_.end()) {
        return;
    }

    selected_client_ = normalized_id;
    for (auto& [id, client] : clients_) {
        client.selected = (id == normalized_id);
    }
}

void ClientRegistry::set_floating(const std::string& client_id, bool floating) {
    const auto it = clients_.find(normalize_client_id(client_id));
    if (it == clients_.end()) {
        return;
    }
    it->second.floating = floating;
    it->second.eligible = is_client_eligible(it->second);
}

bool ClientRegistry::set_managed(const std::string& client_id, bool managed) {
    const auto it = clients_.find(normalize_client_id(client_id));
    if (it == clients_.end()) {
        return false;
    }
    if (managed && !it->second.eligible) {
        return false;
    }

    it->second.managed = managed;
    if (!managed && selected_client_.has_value() && *selected_client_ == it->first) {
        selected_client_ = std::nullopt;
        it->second.selected = false;
    }
    return true;
}

void ClientRegistry::reconcile_management(const std::optional<std::string>& managed_workspace_id) {
    for (auto& [_, client] : clients_) {
        if (!managed_workspace_id.has_value() || client.workspace_id != *managed_workspace_id || !client.eligible) {
            client.managed = false;
        }
    }

    if (selected_client_.has_value()) {
        const auto selected_it = clients_.find(*selected_client_);
        if (selected_it == clients_.end() || !selected_it->second.managed) {
            selected_client_ = std::nullopt;
            for (auto& [_, client] : clients_) {
                client.selected = false;
            }
        }
    }
}

const ClientRecord* ClientRegistry::find(const std::string& client_id) const {
    const auto it = clients_.find(normalize_client_id(client_id));
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
