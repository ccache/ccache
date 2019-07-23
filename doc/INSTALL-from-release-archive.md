ccache installation from release archive
========================================

Prerequisites
-------------

To build ccache from a
[release archive](https://ccache.dev/download.html), you need:

- A C compiler (for instance GCC).
- [libb2](https://github.com/BLAKE2/libb2). If you don't have libb2 installed
  and can't or don't want to install it on your system, you can pass
  `--with-libb2-from-internet` to the configure script, which will make the
  script download libb2 from the Internet and unpack it in the local source
  tree. ccache will then be linked statically to the locally built libb2.
- [libzstd](https://www.zstd.net). If you don't have libzstd installed and
  can't or don't want to install it on your system, you can pass
  `--with-libzstd-from-internet` to the configure script, which will make the
  script download libzstd from the Internet and unpack it in the local source
  tree. ccache will then be linked statically to the locally built libzstd.


Installation
------------

To compile and install ccache, run these commands:

    ./configure
    make
    make install

You may set the installation directory and other parameters by options to
`./configure`. To see them, run `./configure --help`.

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
