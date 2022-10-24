#!/bin/sh

# prevent browser from caching by appending timestamp to urls
ts=$(date +%s)

preflags="
  -I ./thirdparty/
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
# if I ever need to embed data for wasm: --preload-file ./data

dbgflags="-DCUBECALC_DEBUG -DMULTITHREAD_DEBUG"
flags="-fdiagnostics-color=always"
commonbuildflags="-fno-strict-aliasing"
buildflags="-O0" # ~1s build time
units=compilation-units/monolith.c
if which tcc >/dev/null 2>&1; then
  cc=tcc
else
  cc=${CC:-gcc}
fi

is_release=false
serve=true

for x in $@; do
  case "$x" in
    rel*)
      # ~6s build time
      buildflags="-O3 -flto"
      dbgflags=""
      is_release=true
      ;;
    noserv*)
      serve=false
      ;;
    san*) buildflags="-O0 -g -fsanitize=address,undefined,leak" ;;
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
is_emcc=false
if [ "$compiler" = "emcc" ]; then
  is_emcc=true
fi
if ! $is_emcc; then
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

flags="$(echo $preflags $units $platformflags $flags $buildflags $dbgflags $commonbuildflags)"
echo "compiler: $compiler"
echo "flags: $flags"

# NOTE: mold does nothing when building for emscripten
# it will be useful when I build a desktop version

if which mold >/dev/null 2>&1; then
  moldcmd="mold -run"
fi

if $is_emcc; then
  # note: these commands cannot run concurrently if the cache doesn't already exists or needs to
  #       be updated. seems to be a limitation of emcc
  $moldcmd $cc -o main.js $flags $mtflags $wasmflags
  if $is_release; then
    $moldcmd $cc -o main-singlethread.js $flags $stflags $wasmflags
    $moldcmd $cc -o main-nowasm.js $flags $stflags $nowasmflags
  fi
  if $serve; then
    ln -fsv . test
    python -m http.server --directory test 6969
  fi
else
  time $moldcmd $cc $flags || exit
  if $serve; then
    ASAN_OPTIONS=use_sigaltstack=false,symbolize=1 \
    ASAN_SYMBOLIZER_PATH=$(which addr2line) \
    ./cubecalc
  fi
fi
