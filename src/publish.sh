#!/bin/sh

d=~/src/francesco149.github.io/maple/cube/
files="
  index.html
  main.js
  main.worker.js
  main.ww.js
  main.wasm
  main-singlethread.wasm
  main-singlethread.js
  main-nowasm.js
  main-nowasm.js.mem
  coi-serviceworker.js
"
cp $files $d
pushd $d
git add $files
git commit
git push
popd
