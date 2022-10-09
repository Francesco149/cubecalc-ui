#!/bin/sh

d=~/src/francesco149.github.io/maple/cube/
mkdir -pv $d/thirdparty
files="
  index.html
  main.js
  main.worker.js
  main.wasm
  coi-serviceworker.js
"
cp $files $d
pushd $d
git add $files $thirdparty
git commit
git push
popd
