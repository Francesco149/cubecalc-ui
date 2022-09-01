#!/bin/sh

# prevent browser from caching by appending timestamp to urls
ts=$(date +%s)

flags="-sALLOW_MEMORY_GROWTH  -sWASM_BIGINT"
fastbuild="-O0" # ~1s build time
case "$1" in
  rel*) flags="$flags -O3" ;; # ~6s build time
  san*) flags="$flags -O3 -g -fsanitize=address,undefined" ;;
  *) flags="$flags $fastbuild" ;;
esac

echo "flags: $flags"

./generate_c.py > generated.c &&
time emcc \
  -DTS=$ts \
  -o main.js main.c -lGL \
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

#xdg-open "http://0.0.0.0:6969/"
python -m http.server 6969
