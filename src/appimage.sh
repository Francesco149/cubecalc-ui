#!/bin/sh

arch=${ARCH:-x86_64}
rm -rf builddir
meson setup builddir --prefix=$(pwd)/AppDir/usr -Dbuildtype=release &&
cd builddir &&
meson install &&
cd ..

checksum() {
  sha512sum -b "$@" | cut -d' ' -f1
}

getdep() {
  url="$1"
  sum="$2"
  fname=$(basename "$url")
  checksum "$fname" > sum.txt
  echo "$sum" > expected-sum.txt
  cmp sum.txt expected-sum.txt || curl -OL "$url"
  rm sum.txt expected-sum.txt
}

release="1-alpha-20220822-1"
baseurl="https://github.com/linuxdeploy/linuxdeploy/releases/download/$release/"

case $arch in
  x86_64)
    sum="bcba845a8d04555afb689fa25bde512629387857a5331f6fa7fac3c3fa30993a65f5f5717cb8d4c8aeaa5f32e5a1b8a19a3e3d489e9fa08add967e165a6e0d9f"
    ;;
  i386)
    sum="6aab4496c7ce548b6d56cb74cd1fd38c2cb9520f89941022b9be6be7d508dd3124d0159dcf94f4d27ea48d2edffc0976eb11f3649c8088253c0ba90fa840a7aa"
    ;;
esac


exename="linuxdeploy-${arch}.AppImage"
getdep "${baseurl}${exename}" "$sum"
chmod +x ./"$exename"
./"$exename" --appdir AppDir --output appimage
mv ./CubeCalcUI-*-"${arch}.AppImage" "cubecalc-ui-linux-${arch}.AppImage"
