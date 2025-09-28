# Ccache installation

## Prerequisites

To build ccache you need:

- CMake 3.15 or newer.
- A C++17 compiler. See [Supported platforms, compilers and
  languages](https://ccache.dev/platform-compiler-language-support.html) for
  details.
- A C99 compiler.
- Various software libraries, see _Dependencies_ below.

Optional:

- [Bash](https://www.gnu.org/software/bash) and [Python](https://www.python.org)
  to run the test suite.
- [Asciidoctor](https://asciidoctor.org) to build documentation in HTML and
  Markdown format.
- [Pandoc](https://pandoc.org) to build documentation in Markdown format.

See also [CI configurations](../.github/workflows/build.yaml) for regularly
tested build setups, including cross-compilation and dependencies required in
Debian/Ubuntu environments.

## Software library dependencies

### How to locate or retrieve

The CMake variable `DEPS` can be set to select how software library dependencies
should be located or retrieved:

- `AUTO` (the default): Use dependencies from the local system if available.
  Otherwise: Use bundled dependencies if available. Otherwise: Download
  dependencies from the internet (dependencies will then be linked statically).
- `DOWNLOAD`: Use bundled dependencies if available. Otherwise: Download
  dependencies from the internet (dependencies will then be linked
  statically).
- `LOCAL`: Use dependencies from the local system if available. Otherwise: Use
  bundled dependencies if available.

### Dependencies

- [BLAKE3](https://github.com/BLAKE3-team/BLAKE3)[^1]
- [cpp-httplib](https://github.com/yhirose/cpp-httplib)[^1] (optional, disable
  with `-D HTTP_STORAGE_BACKEND=OFF`)
- [doctest](https://github.com/doctest/doctest)[^2] (optional, disable with `-D
  ENABLE_TESTING=OFF`)
- [fmt](https://fmt.dev)[^1]
- [hiredis](https://github.com/redis/hiredis)[^2] (optional, disable with `-D
  REDIS_STORAGE_BACKEND=OFF`)
- [span-lite](https://github.com/martinmoene/span-lite)[^1]
- [tl-expected](https://github.com/TartanLlama/expected)[^1]
- [xxhash](https://github.com/Cyan4973/xxHash)[^2]
- [Zstandard](https://github.com/facebook/zstd)[^2]

[^1]: A bundled version will be used if missing locally.
[^2]: A downloaded version will be used if missing locally.

### Tips

- To make CMake search for libraries in a custom location, use `-D
  CMAKE_PREFIX_PATH=/some/custom/path`.
- To link libraries statically, pass `-D STATIC_LINK=ON` to CMake (this is the
  default on Windows). Alternatively, use `-D
  EXAMPLE_LIBRARY=/path/to/libexample.a` to link statically with libexample.

## Installation

Here is a typical way to build and install ccache:

```bash
mkdir build
cd build
cmake -D CMAKE_BUILD_TYPE=Release ..
make
make install
```

You can set the installation directory to e.g. `/usr` by adding `-D
CMAKE_INSTALL_PREFIX=/usr` to the CMake command. You can set the directory where
the system configuration file should be located to e.g. `/etc` by adding `-D
CMAKE_INSTALL_SYSCONFDIR=/etc`.

There are two different ways to use ccache to cache a compilation:

1. Prefix your compilation command with `ccache`. This method is most convenient
   if you just want to try out ccache or wish to use it for some specific
   projects.
2. Let ccache masquerade as the compiler. This method is most useful when you
   wish to use ccache for all your compilations. To do this, create a symbolic
   link to ccache named as the compiler. For example, here is how to set up
   ccache to masquerade as `gcc` and `g++`:

   ```bash
   cp ccache /usr/local/bin/
   ln -s ccache /usr/local/bin/gcc
   ln -s ccache /usr/local/bin/g++
   ```

   On platforms that don't support symbolic links you can simply copy ccache to the
   compiler name instead for a similar effect:

   ```bash
   cp ccache /usr/local/bin/gcc
   cp ccache /usr/local/bin/g++
   ```

   And so forth. This will work as long as the directory with symbolic links or
   ccache copies comes before the directory with the compiler (typically
   `/usr/bin`) in `PATH`.
