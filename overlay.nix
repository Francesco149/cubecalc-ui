self: super: with super; let
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
in {

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
    version = "0.3.0-dev";
    src = ./src;

    nativeBuildInputs = [
      meson
      ninja
      pkg-config
    ];

    buildInputs = [
      glfw3
    ];

    inherit meta;
  };

  cubecalc-ui-web = pkgs.buildEmscriptenPackage rec {
    pname = "cubecalc-ui-web";
    version = "0.3.0-dev";
    src = ./src;

    nativeBuildInputs = [
      emscripten
    ];

    configurePhase = ''
    '';

    buildPhase = ''
      ./build.sh emcc rel noserv
    '';

    outputs = [ "out" ];

    installPhase = ''
      mkdir -p $out
      mv -v *.html $out/
      mv -v *.js $out/
      mv -v *.mem $out/
      mv -v *.wasm $out/
    '';

    checkPhase = ''
    '';

    inherit meta;
  };

  cubecalc-ui-devshell = mkShell rec {
    buildInputs = [
      emscripten
      actionlint

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
      lines =
        builtins.map (x: "export LD_LIBRARY_PATH=\"${x}/lib:$LD_LIBRARY_PATH\"") buildInputs ++
        # this is necessary when using address sanitizer. it finds some other libGLX otherwise
        [ "export LD_LIBRARY_PATH=\"/run/opengl-driver/lib/:$LD_LIBRARY_PATH\"" ];
    in
      lib.concatStringsSep "\n" lines;
  };

}
