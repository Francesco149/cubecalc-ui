#!/bin/sh

# prevent browser from caching by appending timestamp to urls
ts=$(date +%s)

preflags="
  -I ./thirdparty/
  -DTS=$ts
"
platformflags="
  -s USE_WEBGL2=1
  -s USE_GLFW=3
  -s FULL_ES2
  --cache ./emcc-cache
  -sALLOW_MEMORY_GROWTH
  -lidbfs.js
  -sEXPORTED_FUNCTIONS=_main,_storageAfterInit,_storageAfterCommit
"
wasmflags="
  -s WASM=1
"
nowasmflags="
  -s WASM=0
  -s LEGACY_VM_SUPPORT=1
  -s MIN_IE_VERSION=11
"
mtflags="
  -sWASM_WORKERS
  -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency+1
  -pthread
"
stflags="
  -DNO_MULTITHREAD
"

dbgflags="-DCUBECALC_DEBUG -DMULTITHREAD_DEBUG"
flags="-fdiagnostics-color=always -lGL"
buildflags="-O0" # ~1s build time
units=compilation-units/monolith.c
if which emcc >/dev/null 2>&1; then
  cc=emcc
else
  cc=${CC:-gcc}
fi

for x in $@; do
  case "$x" in
    rel*)
      # ~6s build time
      buildflags="-O3 -flto"
      dbgflags=""
      ;;
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
    -D_GNU_SOURCE
    -pthread
  "
fi

printargs() {
  echo "$@"
}

flags="$(printargs $preflags $units $platformflags $flags $buildflags $dbgflags)"
echo "compiler: $compiler"
echo "flags: $flags"

# NOTE: mold does nothing when building for emscripten
# it will be useful when I build a desktop version

if which mold >/dev/null 2>&1; then
  moldcmd="mold -run"
fi

if [ "$compiler" = "emcc" ]; then
  $moldcmd $cc -o main.js              $flags $mtflags $wasmflags &
  pid1=$!
  $moldcmd $cc -o main-singlethread.js $flags $stflags $wasmflags &
  pid2=$!
  $moldcmd $cc -o main-nowasm.js       $flags $stflags $nowasmflags &
  pid3=$!

  time for x in $pid1 $pid2 $pid3; do
      wait $x || exit
    done
else
  time $moldcmd $cc $flags || exit
fi
#--preload-file ./data \

if [ "$compiler" = "emcc" ]; then
  sed -i "s/main\.wasm/main.wasm?ts=$ts/g" main.js
  sed -i "s/main-singlethread\.wasm/main-singlethread.wasm?ts=$ts/g" main-singlethread.js
  sed -i "s/main-nowasm\.js\.mem/main-nowasm.js.mem?ts=$ts/g" main-nowasm.js
  sed -i "s/main\.worker\.js/main.worker\.js?ts=$ts/g" main.js
  sed -i '\|buildflags =|c\
      const buildflags ="'"$flags"'"' index.html
  sed -i '\|const ts=|c\
      const ts="'"$ts"'";' index.html
fi

#xdg-open "http://0.0.0.0:6969/"
if [ "$compiler" = "emcc" ]; then
  ln -fsv . test
  python -m http.server --directory test 6969
else
  # I can't get asan to work with glfw. it makes glx fail for some reason
  ./cubecalc
fi
