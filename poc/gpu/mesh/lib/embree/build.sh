#!/bin/bash
# Builds Embree v4.4.1 (static, no ISPC, no TBB) and embree_runner under storage/mesh/embree. Never builds in poc/gpu/build.
set -e
ROOT=/Volumes/CrucialX9/Projects/mongo
S=$ROOT/storage/mesh/embree
HERE=$ROOT/poc/gpu/mesh/lib/embree
export TMPDIR=$ROOT/storage/tmp
[ -d $S/src ] || git clone https://github.com/embree/embree $S/src
git -C $S/src checkout -q f590db83ef6559387df7f6d8725c34fb7acf851d
[ "$(git -C $S/src rev-parse HEAD)" = f590db83ef6559387df7f6d8725c34fb7acf851d ]
cmake -S $S/src -B $S/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$S/install \
  -DEMBREE_ISPC_SUPPORT=OFF -DEMBREE_TASKING_SYSTEM=INTERNAL -DEMBREE_TUTORIALS=OFF -DEMBREE_STATIC_LIB=ON \
  -DEMBREE_GEOMETRY_TRIANGLE=ON -DEMBREE_GEOMETRY_QUAD=OFF -DEMBREE_GEOMETRY_CURVE=OFF -DEMBREE_GEOMETRY_SUBDIVISION=OFF \
  -DEMBREE_GEOMETRY_USER=ON -DEMBREE_GEOMETRY_INSTANCE=OFF -DEMBREE_GEOMETRY_INSTANCE_ARRAY=OFF -DEMBREE_GEOMETRY_GRID=OFF -DEMBREE_GEOMETRY_POINT=OFF \
  -DEMBREE_FILTER_FUNCTION=ON
time cmake --build $S/build -j 3
cmake --install $S/build
# runner: static libs; link everything Embree installed
c++ -std=c++17 -O2 -I$S/install/include -I$ROOT/poc/gpu/mesh/harness $HERE/runner.cpp -o $S/embree_runner \
  -L$S/install/lib -lembree4 -lembree_avx2 -lsimd -llexers -lmath -lsys -ltasking -lembree4 -lembree_avx2 -lsimd -lsys -lmath -ltasking
c++ -std=c++17 -O2 -I$ROOT/poc/gpu/mesh/harness $HERE/genpoints.cpp -o $S/genpoints
c++ -std=c++17 -O2 -I$ROOT/poc/gpu/mesh/harness $HERE/refpip.cpp -o $S/refpip
c++ -std=c++17 -O2 -I$ROOT/poc/gpu/mesh/harness $HERE/nearsurface.cpp -o $S/nearsurface
