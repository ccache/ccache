ccache installation from source repository
==========================================

Prerequisites
-------------

To build ccache from a source repository or from a
[release archive](https://ccache.dev/download.html), you need:

- A C++11 compiler.
- A C99 compiler.
- GNU Bourne Again SHell (bash) for tests.
- [AsciiDoc](https://www.methods.co.nz/asciidoc/) to build the HTML
  documentation.
- [xsltproc](http://xmlsoft.org/XSLT/xsltproc2.html) to build the man page.
- [Autoconf](https://www.gnu.org/software/autoconf/) to generate the configure
  script and related files.
- [libb2](https://github.com/BLAKE2/libb2). If you don't have libb2 installed
  and can't or don't want to install it on your system, you can pass
  `-DUSE_LIBB2_FROM_INTERNET=ON` to cmake, which will download libb2 from the
  Internet and unpack it in the local binary tree.
  ccache will then be linked statically to the locally built libb2.
- [libzstd](https://www.zstd.net). If you don't have libzstd installed and
  can't or don't want to install it on your system, you can pass
  `-DUSE_LIBZSTD_FROM_INTERNET=ON` to cmake, which will download libzstd from
  the Internet and unpack it in the local binary tree.
  ccache will then be linked statically to the locally built libzstd.

To debug and run the performance test suite you'll also need:

- [Python](https://www.python.org)


Installation
------------

To compile and install ccache, run these commands:

    mkdir build && cd build
    cmake ..
    make
    make install

You may set the installation directory to e.g. /usr by replacing above cmake
call with with `cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr ..`.

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
