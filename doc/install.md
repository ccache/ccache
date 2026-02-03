# Ccache installation

## Prerequisites

**Required:**

- CMake 3.15 or newer
- A C++20 compiler (see [Supported platforms, compilers and
  languages](https://ccache.dev/platform-compiler-language-support.html) for
  details)
- A C99 compiler
- Software libraries listed in _Dependencies_ below

**Optional:**

- [Bash](https://www.gnu.org/software/bash) and [Python](https://www.python.org)
  to run the test suite
- [Asciidoctor](https://asciidoctor.org) to build documentation in HTML and
  Markdown formats
- [Pandoc](https://pandoc.org) to build documentation in Markdown format

See also [CI configurations](../.github/workflows/build.yaml) for regularly
tested build setups, including cross-compilation and dependencies required in
Debian/Ubuntu environments.

## Software library dependencies

### How to locate or retrieve dependencies

The CMake variable `DEPS` can be set to select how software library dependencies
should be located or retrieved:

- `AUTO` (default): Use dependencies from the local system if available,
  otherwise use bundled dependencies if available, otherwise download
  dependencies from the internet (dependencies will then be linked statically)
- `DOWNLOAD`: Use bundled dependencies if available, otherwise download
  dependencies from the internet (dependencies will then be linked statically)
- `LOCAL`: Use dependencies from the local system if available, otherwise use
  bundled dependencies if available

### Dependencies

**Required libraries:**

- [BLAKE3](https://github.com/BLAKE3-team/BLAKE3)[^1]
- [fmt](https://fmt.dev)[^1]
- [tl-expected](https://github.com/TartanLlama/expected)[^1]
- [xxhash](https://github.com/Cyan4973/xxHash)[^2]
- [Zstandard](https://github.com/facebook/zstd)[^2]

**Optional libraries:**

- [cpp-httplib](https://github.com/yhirose/cpp-httplib)[^1] (disable with `-D
  HTTP_STORAGE_BACKEND=OFF`)
- [doctest](https://github.com/doctest/doctest)[^2] (disable with `-D
  ENABLE_TESTING=OFF`)
- [hiredis](https://github.com/redis/hiredis)[^2] (disable with `-D
  REDIS_STORAGE_BACKEND=OFF`)

[^1]: A bundled version will be used if missing locally.
[^2]: A downloaded version will be used if missing locally.

### Configuration tips

- To make CMake search for libraries in a custom location, use `-D
  CMAKE_PREFIX_PATH=/some/custom/path`.
- To link libraries statically, pass `-D STATIC_LINK=ON` to CMake (this is the
  default on Windows). Alternatively, use `-D
  EXAMPLE_LIBRARY=/path/to/libexample.a` to link statically with a specific
  library.

## Installation

### Basic build and installation

Here is the typical way to build and install ccache:

```bash
mkdir build
cd build
cmake -D CMAKE_BUILD_TYPE=Release ..
make
make install
```

### Common configuration options

- Set the installation directory (e.g., to `/usr`): add `-D
  CMAKE_INSTALL_PREFIX=/usr`
- Set the system configuration file location (e.g., to `/etc`): add `-D
  CMAKE_INSTALL_SYSCONFDIR=/etc`

### Using ccache

There are two different ways to use ccache to cache compilations:

#### Method 1: Prefix compilation commands with ccache

This method is most convenient if you want to try out ccache or use it for
specific projects only.

```bash
ccache gcc -c hello.c
ccache g++ -c hello.cpp
```

#### Method 2: Let ccache masquerade as the compiler

This method is most useful when you want ccache to automatically cache all your
compilations. Ccache will intercept compiler calls and handle caching
transparently.

On platforms with symbolic link support:

```bash
cp ccache /usr/local/bin/
ln -s ccache /usr/local/bin/gcc
ln -s ccache /usr/local/bin/g++
```

On platforms without symbolic link support, simply copy ccache to the compiler
name:

```bash
cp ccache /usr/local/bin/gcc
cp ccache /usr/local/bin/g++
```

**Important:** The directory containing the ccache symbolic links or copies
(e.g., `/usr/local/bin`) must come before the directory with the actual compiler
(typically `/usr/bin`) in your `PATH` environment variable.
