#include <string>

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

    return {kPluginName, kPluginDescription, kPluginAuthor, kPluginVersion};
}

APICALL EXPORT void PLUGIN_EXIT() {}
#else
// Fallback bootstrap path for agent sandboxes that do not expose Hyprland headers.
// The real nested-Hyprland validation must still be done in the user's Nix env.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE) {
    return {
        kPluginName,
        std::string {kPluginDescription} + " Built without Hyprland headers.",
        kPluginAuthor,
        kPluginVersion,
    };
}

APICALL EXPORT void PLUGIN_EXIT() {}
#endif
