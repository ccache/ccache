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
- [libzstd](http://www.zstd.net). If you don't have libzstd installed and can't
  or don't want to install it in a standard system location, it will be
  automatically downloaded, built and linked statically as part of the build
  process. To disable this, pass `-DZSTD_FROM_INTERNET=OFF` to `cmake`. You can
  also install zstd in a custom path and pass
  `-DCMAKE_PREFIX_PATH=/some/custom/path` to `cmake`.

  To link libzstd statically (and you have a static libzstd available), pass
  `-DSTATIC_LINK=ON` to `cmake`. This is the default on Windows. Alternatively,
  use `-DZSTD_LIBRARY=/path/to/libzstd.a`.

Optional:

- [hiredis](https://github.com/redis/hiredis) for the Redis storage backend. If
  you don't have libhiredis installed and can't or don't want to install it in a
  standard system location, it will be automatically downloaded, built and
  linked statically as part of the build process. To disable this, pass
  `-DHIREDIS_FROM_INTERNET=OFF` to cmake. You can also install hiredis in a
  custom path and pass `-DCMAKE_PREFIX_PATH=/some/custom/path` to `cmake`.

  To link libhiredis statically (and you have a static libhiredis available),
  pass `-DSTATIC_LINK=ON` to `cmake`. This is the default on Windows.
  Alternatively, use `-DHIREDIS_LIBRARY=/path/to/libhiredis.a`.
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

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make install
```

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
