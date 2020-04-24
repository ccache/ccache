#!/bin/sh -ex
# This script is used by travis.yml and docker.sh

if [ -n "${SPECIAL}" ]; then
  sh ci/${SPECIAL}.sh
else
  mkdir -p build
  cd build
  ${CMAKE_PREFIX:-} cmake ${CCACHE_LOC:-..} ${CMAKE_PARAMS:-}
  # 4 threads seems a reasonable default for Travis
  ${CMAKE_PREFIX:-} cmake --build . ${BUILDEXTRAFLAGS:-} -- -j4
  test "${RUN_TESTS:-1}" -eq "1" && ctest --output-on-failure -j4
fi
exit 0
