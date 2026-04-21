{
  hyprlandPlugins,
  cmake,
  pkg-config,
  lib,
  hyprland,
}:
hyprlandPlugins.mkHyprlandPlugin (finalAttrs: {
  pluginName = "hyprmacs";
  version = "0.1.0";
  src = ../plugin;
  inherit hyprland;

  nativeBuildInputs = [
    cmake
    pkg-config
  ];

  cmakeFlags = [
    "-DHYPRMACS_BUILD_TESTS=OFF"
  ];

  meta = with lib; {
    description = "hyprmacs bootstrap Hyprland plugin";
    license = licenses.mit;
    platforms = platforms.linux;
  };
})
