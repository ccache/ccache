Ccache installation
===================

Prerequisites
-------------

To build ccache you need:

- CMake 3.15 or newer.
- A C++17 compiler. See [Supported platforms, compilers and
  languages](https://ccache.dev/platform-compiler-language-support.html) for
  details.
- A C99 compiler.
- [libzstd](http://www.zstd.net). If you don't have libzstd installed and
  can't or don't want to install it in a standard system location, there are
  two options:

    1. Install zstd in a custom path and set `CMAKE_PREFIX_PATH` to it, e.g.
       by passing `-DCMAKE_PREFIX_PATH=/some/custom/path` to `cmake`, or
    2. Pass `-DZSTD_FROM_INTERNET=ON` to `cmake` to make it download libzstd
       from the Internet and unpack it in the local binary tree. Ccache will
       then be linked statically to the locally built libzstd.

  To link libzstd statically you can use `-DZSTD_LIBRARY=/path/to/libzstd.a`.

Optional:

- [hiredis](https://github.com/redis/hiredis) for the Redis storage backend. If
  you don't have libhiredis installed and can't or don't want to install it in a
  standard system location, there are two options:

    1. Install hiredis in a custom path and set `CMAKE_PREFIX_PATH` to it, e.g.
       by passing `-DCMAKE_PREFIX_PATH=/some/custom/path` to `cmake`, or
    2. Pass `-DHIREDIS_FROM_INTERNET=ON` to `cmake` to make it download hiredis
       from the Internet and unpack it in the local binary tree. Ccache will
       then be linked statically to the locally built libhiredis.

  To link libhiredis statically you can use
  `-DHIREDIS_LIBRARY=/path/to/libhiredis.a`.
- GNU Bourne Again SHell (bash) for tests.
- [Asciidoctor](https://asciidoctor.org) to build the HTML documentation.
- [Python](https://www.python.org) to debug and run the performance test suite.

Reference configurations:

- See [CI configurations](../.github/workflows/build.yaml) for a selection of
  regularly tested build setups, including cross-compiling and explicit
  dependencies required in Debian/Ubuntu environment.

Installation
------------

Here is the typical way to build and install ccache:

    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make
    make install

You can set the installation directory to e.g. `/usr` by adding
`-DCMAKE_INSTALL_PREFIX=/usr` to the `cmake` command. You can set the directory
where the secondary configuration file should be located to e.g. `/etc` by
adding `-DCMAKE_INSTALL_SYSCONFDIR=/etc`.

There are two different ways to use ccache to cache a compilation:

1. Prefix your compilation command with `ccache`. This method is most convenient
   if you just want to try out ccache or wish to use it for some specific
   projects.
2. Let ccache masquerade as the compiler. This method is most useful when you
   wish to use ccache for all your compilations. To do this, create a symbolic
   link to ccache named as the compiler. For example, here is set up ccache to
   masquerade as `gcc` and `g++`:
+
-------------------------------------------------------------------------------
cp ccache /usr/local/bin/
ln -s ccache /usr/local/bin/gcc
ln -s ccache /usr/local/bin/g++
-------------------------------------------------------------------------------
+
On platforms that don't support symbolic links you can simply copy ccache to the
compiler name instead for a similar effect:
+
-------------------------------------------------------------------------------
cp ccache /usr/local/bin/gcc
cp ccache /usr/local/bin/g++
-------------------------------------------------------------------------------
+
And so forth. This will work as long as the directory with symbolic links or
ccache copies comes before the directory with the compiler (typically
`/usr/bin`) in `PATH`.
