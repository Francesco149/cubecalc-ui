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
          devShell
          cubecalc-ui;
      };
      packages = pkgs;
      defaultPackage = pkgs.cubecalc-ui;

      # export pkgs.devShell as devShell. the nix develop / direnv shell
      inherit (pkgs) devShell;

      apps.cubecalc-ui = flake-utils.lib.mkApp { drv = pkgs.cubecalc-ui; };

      # nix friendly CI/builder thing
      hydraJobs = {
        inherit
        (legacyPackages)
        cubecalc-ui;
      };

      checks = { inherit (legacyPackages); };
    }) // {
      # these are for example for when I want to expose a nix module or overlay
      overlay = localOverlay;
      overlays = [];
      nixosModule = { config }: { options = {}; config= {}; };
    };
}
