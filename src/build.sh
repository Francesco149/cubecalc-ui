#!/bin/sh

# NOTE: the use of legacy OpenGL2 is intentional so that if I compile it for desktop it will run
#       on older machines that don't have GL3 support

# prevent browser from caching by appending timestamp to urls
ts=$(date +%s)

flags="-O0 -sWASM_BIGINT" # ~1s build time
if [ "$1" = "release" ]; then
  flags="-O3" # ~6s build time
fi

./generate_c.py > generated.c &&
time emcc \
  -DTS=$ts \
  -o main.js main.c -lGL \
  -s USE_WEBGL2=1 \
  -s USE_GLFW=3 \
  -s LEGACY_GL_EMULATION=1 \
  -s WASM=1 \
  -s ASYNCIFY \
  --cache ./emcc-cache \
  $flags \
  || exit
#--preload-file ./data \

rm ./cubecalc.zip
mkdir -p ./archive/cubecalc/
cd ./archive
cp ../glue.py ./cubecalc/
cp ../init.py ./cubecalc/
cp ../cubecalc/src/{cubecalc,common,datautils,kms,tms}.py ./cubecalc/
zip -r ../cubecalc.zip ./cubecalc
cd ..
rm -rf ./archive/
unzip -l ./cubecalc.zip

sed -i "s/main\.wasm/main.wasm?ts=$ts/g" main.js
sed -i "s/main\.js\(?ts=[0-9]\+\)\?/main.js?ts=$ts/g" index.html

#xdg-open "http://0.0.0.0:6969/"
python -m http.server 6969
