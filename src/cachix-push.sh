#!/bin/sh

push_cache() {
  jq -r '.[].outputs | to_entries[].value' |
  cachix push lolisamurai
}

for x in '.#cubecalc-ui' '.#cubecalc-ui-web'; do
  nix build "$x" --json | push_cache
done
