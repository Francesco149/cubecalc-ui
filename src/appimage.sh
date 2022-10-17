#!/bin/sh

rm -rf builddir
meson setup builddir --prefix=$(pwd)/AppDir/usr -Dbuildtype=release &&
cd builddir &&
meson install &&
cd ..

packagers="
  https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20220822-1/linuxdeploy-x86_64.AppImage
"

for x in $packagers; do
  [ -e "$x" ] || curl -OL $x
  exename=$(basename "$x")
  chmod +x ./"$exename"
  arch=$(echo $exename | sed 's/^linuxdeploy-//; s/\.AppImage$//')
  ./"$exename" --appdir AppDir --output appimage
  mv ./CubeCalcUI-*-"${arch}.AppImage" "cubecalc-ui-${arch}.AppImage"
done
