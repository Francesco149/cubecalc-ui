#!/bin/sh

# prevent browser from caching by appending timestamp to urls
ts=$(date +%s)

flags="-sALLOW_MEMORY_GROWTH  -sWASM_BIGINT -lidbfs.js"
flags="$flags -sEXPORTED_FUNCTIONS=_main,_storageAfterInit,_storageAfterCommit"
buildflags="-O0 -DCUBECALC_DEBUG" # ~1s build time
units=compilation-units/monolith.c
cc=emcc

for x in $@; do
  case "$x" in
    rel*) buildflags="-O3 -flto" ;; # ~6s build time
    san*) buildflags="-O3 -g -fsanitize=address,undefined,leak -DCUBECALC_DEBUG" ;;

    # optionally build as separate compilation units to test that
    # they are not referencing each other in unintended ways.
    # monolith build is usually faster and lets the compiler optimize harder
    # on my machine, monolith build is ~12% faster
    unit*)
      units=compilation-units/*_impl.c
      units="$units main.c"
      ;;
  esac
done

flags="$flags $buildflags"
echo "flags: $flags"

# NOTE: mold does nothing when building for emscripten
# it will be useful when I build a desktop version

./generate_c.py > generated.c &&
protoc -I ./thirdparty/ -I . --c_out=./proto ./cubecalc.proto &&
time mold -run $cc \
  -I ./thirdparty/ \
  -DTS=$ts \
  -o main.js $units -lGL \
  -s USE_WEBGL2=1 \
  -s USE_GLFW=3 \
  -s FULL_ES2 \
  -s WASM=1 \
  -s ASYNCIFY \
  --cache ./emcc-cache \
  $flags \
  || exit
#--preload-file ./data \

rm ./cubecalc.zip
mkdir -p ./archive/cubecalc/
cd ./archive
cp ../{glue{,_common},cubecalc/src/{cubecalc,common,datautils,kms,tms,familiars}}.py ./cubecalc/
zip -r ../cubecalc.zip ./cubecalc
cd ..
rm -rf ./archive/
unzip -l ./cubecalc.zip

sed -i "s/main\.wasm/main.wasm?ts=$ts/g" main.js
sed -i "s/main\.js\(?ts=[0-9]\+\)\?/main.js?ts=$ts/g" index.html
sed -i "/buildflags =/c\
  const buildflags =\"$flags\"" index.html

#xdg-open "http://0.0.0.0:6969/"
python -m http.server 6969
