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
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

    sandbox.url = "github:archie-judd/agent-sandbox.nix";
    llm-agents.url = "github:numtide/llm-agents.nix";
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
      sandbox,
      llm-agents,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        # Official debug package from the Hyprland flake.
        hyprPkg = hyprland.packages.${system}.hyprland-debug;

        commonTools = [
          pkgs.coreutils
          pkgs.which
          pkgs.git
          pkgs.ripgrep
          pkgs.fd
          pkgs.gnused
          pkgs.gnugrep
          pkgs.findutils
          pkgs.jq
          pkgs.cmake
          pkgs.pkg-config
          pkgs.gnumake
          pkgs.clang
        ];

        agentTools = [
          pkgs.emacs
        ];

        devTools = [
          hyprPkg
          pkgs.foot
          pkgs.gdb
          pkgs.wayland-utils
        ];
        codex-sandbox = sandbox.lib.${system}.mkSandbox {
          pkg = llm-agents.packages.${system}.codex;
          binName = "codex";
          outName = "codex-sandbox"; # or whatever alias you'd like
          allowedPackages = commonTools ++ agentTools;
          stateDirs = [
            "$HOME/.codex"
            "$HOME/.agents"
          ];
          stateFiles = [ ];
          extraEnv = { };
          restrictNetwork = false;
        };

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
          runtimeInputs = devTools ++ [
            pkgs.jq
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

        hyprmacsPlugin = pkgs.callPackage ./nix/hyprmacs-plugin.nix {
          hyprland = hyprPkg;
        };

        hyprmacsLoad = pkgs.writeShellApplication {
          name = "hyprmacs-load";
          runtimeInputs = [
            hyprPkg
            pkgs.findutils
            pkgs.gnugrep
          ];
          text = ''
            so_path="$(find ${hyprmacsPlugin}/lib -maxdepth 1 -type f -name '*.so' | head -n 1)"
            if [ -z "$so_path" ]; then
              echo "No plugin shared object found under ${hyprmacsPlugin}/lib"
              exit 1
            fi

            if strings "$so_path" | grep -Eq "bootstrap-fallback|Built without Hyprland headers"; then
              echo "Refusing to load fallback plugin build from $so_path"
              echo "Rebuild the plugin package in an environment with Hyprland headers."
              exit 1
            fi

            hyprctl plugin load "$so_path"
          '';
        };
      in
      {
        packages.default = runNested;
        packages.codex-sandbox = codex-sandbox;
        packages.hyprmacs-plugin = hyprmacsPlugin;
        packages.hyprmacs-load = hyprmacsLoad;

        apps.default = {
          type = "app";
          program = "${runNested}/bin/run-nested-hyprland-debug";
        };
        apps.codex-sandbox = {
          type = "app";
          program = "${codex-sandbox}/bin/codex-sandbox";
        };
        apps.hyprmacs-plugin = {
          type = "app";
          program = "${hyprmacsPlugin}";
        };
        apps.hyprmacs-load = {
          type = "app";
          program = "${hyprmacsLoad}/bin/hyprmacs-load";
        };

        devShells.default = pkgs.mkShell {
          packages = commonTools ++ devTools ++ [
            runNested
            codex-sandbox
          ];
        };
      }
    );
}
