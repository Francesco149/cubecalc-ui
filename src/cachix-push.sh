#!/bin/sh

nix build --json |
  jq -r '.[].outputs | to_entries[].value' |
  cachix push lolisamurai
