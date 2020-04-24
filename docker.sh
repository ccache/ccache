#!/bin/sh

# This script will run misc/build.sh within a docker image.
# ToDo: use ccache within docker and preserve the cache.

# Parameter:
#  First parameter     directory name within buildenv, defaults to travis
#  Other parameters    will be passed to misc/build.sh

echo "Warning: Docker support is rather experimental\n"

BUILDENV=${1:-travis}

# expose remaining parameters as $*
shift $(( $# > 0 ? 1 : 0 ))

DOCKER_EXE=docker
DOCKER_IMAGE_TAG=ccache/build:${BUILDENV}-2.0

# Build (if not exists):
${DOCKER_EXE} build -t ${DOCKER_IMAGE_TAG} buildenv/${BUILDENV}

# Run:
echo "Executing: ${DOCKER_EXE} run --rm \\
  --volume ${PWD}:/source \\
  --tmpfs /builddir:rw,exec \\
  --workdir /builddir \\
  --env CC=\"${CC:-}\" \\
  --env CFLAGS=\"${CFLAGS:-}\" \\
  --env CXX=\"${CXX:-}\" \\
  --env CXXFLAGS=\"${CXXFLAGS:-}\" \\
  --env LDFLAGS=\"${LDFLAGS:-}\" \\
  --env ASAN_OPTIONS=\"${ASAN_OPTIONS:-}\" \\
  --env CCACHE_LOC=\"/source\" \\
  --env SPECIAL=\"${SPECIAL:-}\" \\
  --env SCAN_BUILD=\"${SCAN_BUILD:-}\" \\
  --env CONFIGURE=\"${CONFIGURE:-}\" \\
  --env BUILDEXTRAFLAGS=\"${BUILDEXTRAFLAGS:-}\" \\
  --env NO_TEST=\"${NO_TEST:-}\" \\
  ${DOCKER_IMAGE_TAG} \\
  /source/ci/build.sh $*\n\n"

${DOCKER_EXE} run --rm \
  --volume ${PWD}:/source \
  --tmpfs /builddir:rw,exec \
  --workdir /builddir \
  --env CC="${CC:-}" \
  --env CFLAGS="${CFLAGS:-}" \
  --env CXX="${CXX:-}" \
  --env CXXFLAGS="${CXXFLAGS:-}" \
  --env LDFLAGS="${LDFLAGS:-}" \
  --env ASAN_OPTIONS="${ASAN_OPTIONS:-}" \
  --env CCACHE_LOC="/source" \
  --env SPECIAL="${SPECIAL:-}" \
  --env SCAN_BUILD="${SCAN_BUILD:-}" \
  --env CONFIGURE="${CONFIGURE:-}" \
  --env BUILDEXTRAFLAGS="${BUILDEXTRAFLAGS:-}" \
  --env NO_TEST="${NO_TEST:-}" \
  ${DOCKER_IMAGE_TAG} \
  /source/ci/build.sh $*
