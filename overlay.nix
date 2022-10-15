self: super: with super; {

  self.maintainers = super.maintainers.override {
    lolisamurai = {
      email = "lolisamurai@animegirls.win";
      github = "Francesco149";
      githubId = 973793;
      name = "Francesco Noferi";
    };
  };

  cubecalc-ui = stdenv.mkDerivation rec {
    pname = "cubecalc-ui";
    version = "0.2.0-dev";
    src = ./src;

    nativeBuildInputs = [
      meson
      ninja
      pkg-config
    ];

    buildInputs = [
      glfw3
    ];

    meta = with lib; {
      description = "MapleStory average cubing probabilities calculator, advanced graph UI";
      longDescription = ''
        cubecalc-ui estimates the average probability to roll any desired combination
        of stats for cubes and familiars (including some TMS cubes such as Equality, Violet, Uni).
        The graph UI with operators allows configuring for any complicated combination of lines
        as well as showing the matching combinations of lines.
      '';
      homepage = "https://github.com/Francesco149/cubecalc-ui";
      license = licenses.unlicense;
      maintainers = with maintainers; [ lolisamurai ];
      platforms = platforms.all;
    };
  };

  devShell = mkShell rec {
    buildInputs = [
      emscripten

      # to regen protobuf stuff
      protobuf
      protobufc

      # for the web server
      python310

      # for desktop
      libGL
      glfw3
      pkg-config
      mold
      tinycc
      clang

      meson
      ninja

      # cachix
      jq
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
