#!/bin/sh

push_cache() {
  jq -r '.[].outputs | to_entries[].value' |
  cachix push lolisamurai
}

for x in '.#cubecalc-ui' '.#cubecalc-ui-web'; do
  rm tmp.json
  nix build "$x" --json > tmp.json || exit
  push_cache < tmp.json || exit
done

rm tmp.json
