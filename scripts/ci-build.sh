#!/usr/bin/env bash

set -eu
set -o pipefail

if [[ ${1:-false} == false ]]; then
    echo "please provide install prefix as first arg"
    exit 1
fi

INSTALL_PREFIX=$1

export CURRENT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# ensure we start inside the osrm-backend directory (one level up)
cd ${CURRENT_DIR}/../

if [[ `which pkg-config` ]]; then
    echo "Success: Found pkg-config";
else
    echo "echo you need pkg-config installed";
    exit 1;
fi;


MASON_HOME=$(pwd)/mason_packages/.link

function main() {
    if [[ -d build ]]; then
        echo "$(pwd)/build already exists, please delete before re-running"
        exit 1
    fi
    # setup mason and deps
    ./bootstrap.sh
    source ./scripts/install_node.sh 4
    # put mason installed ccache on PATH
    # then osrm-backend will pick it up automatically
    export CCACHE_VERSION="3.2.4"
    ./.mason/mason install ccache ${CCACHE_VERSION}
    export PATH=$(./.mason/mason prefix ccache ${CCACHE_VERSION})/bin:${PATH}
    # put mason installed clang 3.8.0 on PATH
    export PATH=$(./.mason/mason prefix clang 3.8.0)/bin:${PATH}
    export CC=clang-3.8
    export CXX=clang++-3.8
    CMAKE_EXTRA_ARGS=""
    if [[ ${AR:-false} != false ]]; then
        CMAKE_EXTRA_ARGS="${CMAKE_EXTRA_ARGS} -DCMAKE_AR=${AR}"
    fi
    if [[ ${RANLIB:-false} != false ]]; then
        CMAKE_EXTRA_ARGS="${CMAKE_EXTRA_ARGS} -DCMAKE_RANLIB=${RANLIB}"
    fi
    if [[ ${NM:-false} != false ]]; then
        CMAKE_EXTRA_ARGS="${CMAKE_EXTRA_ARGS} -DCMAKE_NM=${NM}"
    fi
    mkdir build && cd build
    ${MASON_HOME}/bin/cmake ../ -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
      -DCMAKE_CXX_COMPILER="${CXX}" \
      -DBoost_NO_SYSTEM_PATHS=ON \
      -DTBB_INSTALL_DIR=${MASON_HOME} \
      -DCMAKE_INCLUDE_PATH=${MASON_HOME}/include \
      -DCMAKE_LIBRARY_PATH=${MASON_HOME}/lib \
      -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
      -DBoost_USE_STATIC_LIBS=ON \
      -DBUILD_TOOLS=1 \
      -DENABLE_CCACHE=ON \
      -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS:-OFF} \
      -DCOVERAGE=${COVERAGE:-OFF} \
      ${CMAKE_EXTRA_ARGS}

    make --jobs=${JOBS}
    make tests --jobs=${JOBS}
    make benchmarks --jobs=${JOBS}
    make install
    export PKG_CONFIG_PATH=${INSTALL_PREFIX}/lib/pkgconfig
    cd ../
    mkdir -p example/build
    cd example/build
    ${MASON_HOME}/bin/cmake ../ -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
    make
    cd ../../
    make -C test/data benchmark
    ./example/build/osrm-example test/data/monaco.osrm
    cd build
    ./unit_tests/library-tests ../test/data/monaco.osrm
    ./unit_tests/extractor-tests
    ./unit_tests/engine-tests
    ./unit_tests/util-tests
    ./unit_tests/server-tests
    cd ../
    npm test
}

main