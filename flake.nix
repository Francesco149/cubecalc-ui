{
  description = "UI for the maplestory cubing averages calculations";
  nixConfig.bash-prompt = "\[cubecalc-ui-dev\]$ ";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs";
    flake-utils.url = "github:numtide/flake-utils";
    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-compat, flake-utils }:
    let
      localOverlay = import ./overlay.nix;
      pkgsForSystem = system: import nixpkgs {
        overlays = [
          localOverlay
        ];
        inherit system;
      };
    in flake-utils.lib.eachDefaultSystem (system: rec {
      legacyPackages = pkgsForSystem system;
      pkgs = flake-utils.lib.flattenTree {
        inherit
          (legacyPackages)
          cubecalc-ui-devshell
          cubecalc-ui;
      };
      packages = pkgs;
      defaultPackage = pkgs.cubecalc-ui;

      # export devShell for nix develop / direnv shell
      devShell = pkgs.cubecalc-ui-devshell;

      # for nix run
      apps.cubecalc-ui = flake-utils.lib.mkApp { drv = pkgs.cubecalc-ui; };
    }) // {
      # these are for example for when I want to expose a nix module or overlay
      overlay = localOverlay;
      overlays = [];
      nixosModule = { config }: { options = {}; config= {}; };
    };
}
