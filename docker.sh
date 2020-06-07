#!/bin/sh -xe

# This script will run misc/build.sh within a docker image.
# ToDo: use ccache within docker and preserve the cache.

# Parameter:
#  First parameter     directory name within buildenv, defaults to travis
#  Other parameters    will be passed to misc/build.sh

echo "Warning: Docker support is rather experimental\n"

BUILDENV=${1:-travis}

# expose remaining parameters as $*
shift $(( $# > 0 ? 1 : 0 ))

# Pulling the docker image is actually slower than just creating it locally!
# (comparison on same machine: ~90 sec for pulling but only ~50secs for building locally)
DOCKER_IMAGE_TAG=alexanderlanin/ccache:${BUILDENV}-1

# Build (if not exists):
docker build -t ${DOCKER_IMAGE_TAG} buildenv/${BUILDENV}

# Cache compilation across docker sessions
# ToDo: separate cache for each docker image or is it fine like that?
mkdir -p build
mkdir -p build/docker-ccache

docker run --rm \
  --volume ${PWD}:/source \
  --volume ${PWD}/build/docker-ccache:/ccache \
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
  --env CMAKE_PARAMS="${CMAKE_PARAMS:-}" \
  --env BUILDEXTRAFLAGS="${BUILDEXTRAFLAGS:-}" \
  --env NO_TEST="${NO_TEST:-}" \
  --env CCACHE_DIR=/ccache \
  ${DOCKER_IMAGE_TAG} \
  /source/ci/build.sh $*
