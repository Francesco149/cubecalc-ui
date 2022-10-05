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
    flake-utils.lib.eachDefaultSystem (system:
      with nixpkgs.legacyPackages.${system}; {
        devShell = mkShell rec {
          buildInputs = [
            emscripten
            mold
            protobuf
            protobufc

            # for desktop
            libGL
            glfw3
            pkg-config
            tinycc
            clang
          ] ++ (with xorg; [
            libX11
            libXau
            libXdmcp
          ]);

          # this is for the tinycc-built binary which doesn't have the nix runpath
          # since we don't use a built system atm, the gcc/clang binary also needs this
          shellHook = let
            lines = builtins.map (x: "export LD_LIBRARY_PATH=\"${x}/lib:$LD_LIBRARY_PATH\"") buildInputs;
          in
            lib.concatStringsSep "\n" lines;
        };
      }
    );
}
