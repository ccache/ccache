Ccache installation
===================

Prerequisites
-------------

To build ccache you need:

- CMake 3.10 or newer.
- A C++14 compiler. See [Supported platforms, compilers and
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

There are two ways to use ccache. You can either prefix your compilation
commands with `ccache` or you can create a symbolic link (named as your
compiler) to ccache. The first method is most convenient if you just want to
try out ccache or wish to use it for some specific projects. The second method
is most useful for when you wish to use ccache for all your compilations.

To install for usage by the first method just copy ccache to somewhere in your
path.

To install for the second method, do something like this:

    cp ccache /usr/local/bin/
    ln -s ccache /usr/local/bin/gcc
    ln -s ccache /usr/local/bin/g++
    ln -s ccache /usr/local/bin/cc
    ln -s ccache /usr/local/bin/c++

And so forth. This will work as long as `/usr/local/bin` comes before the path
to the compiler (which is usually in `/usr/bin`). After installing you may wish
to run `which gcc` to make sure that the correct link is being used.

NOTE: Do not use a hard link, use a symbolic link. A hard link will cause
"interesting" problems.
