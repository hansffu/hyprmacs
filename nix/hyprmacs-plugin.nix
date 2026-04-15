{
  hyprlandPlugins,
  hyprland,
  cmake,
  pkg-config,
  lib,
}:
hyprlandPlugins.mkHyprlandPlugin (
  finalAttrs: {
    pluginName = "hyprmacs";
    version = "0.1.0";
    src = ../plugin;

    nativeBuildInputs = [
      cmake
      pkg-config
    ];

    # Task 1 bootstrap package: build the shared object only.
    cmakeFlags = [
      "-DHYPRMACS_BUILD_TESTS=OFF"
    ];

    meta = with lib; {
      description = "hyprmacs bootstrap Hyprland plugin";
      license = licenses.mit;
      platforms = platforms.linux;
    };
  }
)
