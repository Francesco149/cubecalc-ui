#!/bin/sh
# this is just a script specifically for my ubuntu wsl to test the windows build
arch=${1:-i686}

rm -rf ~/glfw/build
mkdir ~/glfw/build
cd ~/glfw/build
cmake \
  -DGLFW_BUILD_EXAMPLES=OFF \
  -DGLFW_BUILD_TESTS=OFF \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_TOOLCHAIN_FILE=~/cubecalc-ui/src/$arch-w64-llvm-mingw32.cmake \
  -DCMAKE_INSTALL_PREFIX=/opt/llvm-mingw/$arch-w64-mingw32/ \
  -DCMAKE_BUILD_TYPE=Release \
  ..

sudobin=sudo
${NOSUDO:-false} && sudobin=''

$sudobin make VERBOSE=1 -j$(nproc) install || exit

cd ~/cubecalc-ui/src
PKG_CONFIG_PATH=/opt/llvm-mingw/$arch-w64-mingw32/lib/pkgconfig \
PKG_CONFIG_LIBDIR=/opt/llvm-mingw/$arch-w64-mingw32/lib/pkgconfig \
meson setup builddir --prefix ~/cubecalc-mingw -Dbuildtype=release --cross-file $arch-w64-llvm-mingw32.txt
cd builddir
# workaround for meson bug that is fixed in 0.62.2
sed -i 's/-Wl,--allow-shlib-undefined//g' build.ninja
ninja install || exit
cd ..
rm -rf builddir
${DONT_RUN:-false} || ~/cubecalc-mingw/bin/cubecalc-ui.exe
