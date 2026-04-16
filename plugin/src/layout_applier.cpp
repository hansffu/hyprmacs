#include "hyprmacs/layout_applier.hpp"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>

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
        return false;
    }

    const std::string arg = "special:hyprmacs-hidden,address:" + normalized_client_id;
    const int rc = executor_(arg);
    append_layout_debug_log("hide command rc=" + std::to_string(rc) + " client=" + normalized_client_id + " arg=" + arg);
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
        append_layout_debug_log("show failed (not hidden) client=" + normalized_client_id);
        return false;
    }

    const std::string arg = it->second + ",address:" + normalized_client_id;
    const int rc = executor_(arg);
    append_layout_debug_log("show command rc=" + std::to_string(rc) + " client=" + normalized_client_id + " arg=" + arg);
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
