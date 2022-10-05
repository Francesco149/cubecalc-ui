#!/bin/sh

# prevent browser from caching by appending timestamp to urls
ts=$(date +%s)

preflags="
  -I ./thirdparty/
  -DTS=$ts
"
platformflags="
  -o main.js
  -s USE_WEBGL2=1
  -s USE_GLFW=3
  -s FULL_ES2
  -s WASM=1
  --cache ./emcc-cache
  -sALLOW_MEMORY_GROWTH
  -lidbfs.js
  -sEXPORTED_FUNCTIONS=_main,_storageAfterInit,_storageAfterCommit
"

flags="-fdiagnostics-color=always -lGL -DCUBECALC_DEBUG -DUTILS_STDIO"
buildflags="-O0" # ~1s build time
units=compilation-units/monolith.c
if which emcc >/dev/null 2>&1; then
  cc=emcc
else
  cc=${CC:-gcc}
fi

for x in $@; do
  case "$x" in
    rel*) buildflags="-O3 -flto" ;; # ~6s build time
    san*) buildflags="-O0 -g -fsanitize=leak" ;;
    gen*)
      protoc -I ./thirdparty/ -I . --c_out=./proto ./cubecalc.proto || exit
      ;;

    # optionally build as separate compilation units to test that
    # they are not referencing each other in unintended ways.
    # monolith build is usually faster and lets the compiler optimize harder
    # on my machine, monolith build is ~12% faster
    unit*)
      units=compilation-units/*_impl.c
      units="$units main.c"
      ;;

    *)
      cc="$x"
      ;;
  esac
done

compiler="$("$cc" --version 2>&1 | cut -d' ' -f1 | sed 1q)"
if [ "$compiler" != "emcc" ]; then
  preflags="
    $preflags
    $(pkg-config --cflags --libs-only-L glfw3 gl)
  "
  platformflags="
    -ocubecalc
    -lm
    $(pkg-config --libs glfw3 gl)
  "
fi

printargs() {
  echo "$@"
}

flags="$(printargs $preflags $units $platformflags $flags $buildflags)"
echo "flags: $flags"

# NOTE: mold does nothing when building for emscripten
# it will be useful when I build a desktop version

if which mold >/dev/null 2>&1; then
  time mold -run $cc $flags || exit
else
  $cc $flags || exit
fi
#--preload-file ./data \

if [ "$compiler" = "emcc" ]; then
  sed -i "s/main\.wasm/main.wasm?ts=$ts/g" main.js
  sed -i "s/main\.js\(?ts=[0-9]\+\)\?/main.js?ts=$ts/g" index.html
  sed -i '\|buildflags =|c\
    const buildflags ="'"$flags"'"' index.html
fi

#xdg-open "http://0.0.0.0:6969/"
if [ "$compiler" = "emcc" ]; then
  ln -fsv . test
  python -m http.server --directory test 6969
else
  # I can't get asan to work with glfw. it makes glx fail for some reason
  ./cubecalc
fi
