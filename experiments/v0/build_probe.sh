#!/bin/sh
# THROWAWAY feasibility setup. No system installation or product build system.
set -eu
cd "$(dirname "$0")/../.."
probe_python=${PROBE_PYTHON:-python3}
mkdir -p .probe/tools .probe/upstream
"$probe_python" -m pip install --no-cache-dir --target .probe/tools \
    'cmake==3.31.10' 'pyte==0.8.2' 'wcwidth==0.9.1'
if [ ! -d .probe/upstream/tvterm/.git ]; then
    git clone https://github.com/magiblot/tvterm.git .probe/upstream/tvterm
fi
if [ -n "$(git -C .probe/upstream/tvterm status --porcelain)" ]; then
    echo 'Refusing to change a dirty experimental upstream clone' >&2
    exit 1
fi
git -C .probe/upstream/tvterm checkout --detach 210eb23564da06c358d2623388939bb02f7f3419
git -C .probe/upstream/tvterm submodule update --init --recursive
git -C .probe/upstream/tvterm submodule status --recursive
.probe/tools/cmake/data/bin/cmake -S .probe/upstream/tvterm -B .probe/build -DCMAKE_BUILD_TYPE=Debug
.probe/tools/cmake/data/bin/cmake --build .probe/build --parallel 4
