#!/bin/sh

d=~/src/francesco149.github.io/maple/cube/
mkdir -pv $d/thirdparty
files="
  index.html
  main.js
  main.wasm
  cubecalc.zip
"
cp $files $d
pushd $d
git add $files $thirdparty
git commit
git push
popd
