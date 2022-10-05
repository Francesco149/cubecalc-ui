#!/bin/sh

d=~/src/francesco149.github.io/maple/cube/
mkdir -pv $d/thirdparty
files="
  index.html
  main.js
  main.wasm
"
cp $files $d
pushd $d
git add $files $thirdparty
git commit
git push
popd
