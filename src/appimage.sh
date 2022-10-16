#!/bin/sh
rm -rf builddir
meson setup builddir --prefix=$(pwd)/AppDir/usr -Dbuildtype=release &&
cd builddir &&
meson install &&
cd .. &&
curl -OL https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20220822-1/linuxdeploy-x86_64.AppImage &&
chmod +x ./linuxdeploy-x86_64.AppImage &&
./linuxdeploy-x86_64.AppImage --appdir AppDir --output appimage
