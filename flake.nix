{
  description = "Nested debug Hyprland session";

  nixConfig = {
    extra-substituters = [
      "https://hyprland.cachix.org"
    ];
    extra-trusted-public-keys = [
      "hyprland.cachix.org-1:a7pgxzMz7+chwVL3VqEJfliKcT0A3vC0d0jOuxllygc="
    ];
  };

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    flake-utils.url = "github:numtide/flake-utils";

    hyprland = {
      url = "github:hyprwm/Hyprland";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
      hyprland,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };

        # Official debug package from the Hyprland flake.
        hyprPkg = hyprland.packages.${system}.hyprland-debug;

        hyprDebugConfig = pkgs.writeText "hyprlandd.conf" ''
          monitor = , preferred, auto, 1

          general {
            gaps_in = 4
            gaps_out = 8
            border_size = 2
          }

          decoration {
            rounding = 6
          }

          input {
            kb_layout = no
          }

          misc {
            disable_hyprland_logo = true
            disable_splash_rendering = true
            force_default_wallpaper = 0
          }

          debug {
            disable_logs = false
            gl_debugging = true
          }

          # Nested debug-friendly binds using ALT instead of SUPER
          bind = ALT, Return, exec, ${pkgs.foot}/bin/foot
          bind = ALT, Q, killactive,
          bind = ALT_SHIFT, E, exit,
        '';

        runNested = pkgs.writeShellApplication {
          name = "run-nested-hyprland-debug";
          runtimeInputs = [
            hyprPkg
            pkgs.foot
            pkgs.gdb
            pkgs.jq
            pkgs.wayland-utils
          ];
          text = ''
            echo "Starting nested Hyprland debug session..."
            echo "  ALT+Return   open foot"
            echo "  ALT+Q        close active window"
            echo "  ALT+Shift+E  exit nested Hyprland"

            export XDG_CURRENT_DESKTOP=Hyprland
            export XDG_SESSION_DESKTOP=Hyprland
            export XDG_SESSION_TYPE=wayland

            exec ${hyprPkg}/bin/Hyprland --config ${hyprDebugConfig}
          '';
        };
      in
      {
        packages.default = runNested;

        apps.default = {
          type = "app";
          program = "${runNested}/bin/run-nested-hyprland-debug";
        };

        devShells.default = pkgs.mkShell {
          packages = [
            runNested
            hyprPkg
            pkgs.foot
            pkgs.gdb
            pkgs.jq
            pkgs.wayland-utils
          ];
        };
      }
    );
}
