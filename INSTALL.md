ccache installation
===================

Prerequisites
-------------

To build ccache from the source repository, you need:

- A C compiler (for instance GCC)
- [AsciiDoc](http://www.methods.co.nz/asciidoc/) to build the documentation.
- [Autoconf](http://www.gnu.org/software/autoconf/)
- [gperf](http://www.gnu.org/software/gperf/)
- [xsltproc](http://xmlsoft.org/XSLT/xsltproc2.html)

It is also recommended that you have:

- [zlib](http://www.zlib.net) (if you don't have zlib installed, ccache will
  use a bundled copy)

To debug and run the performance test suite you'll also need:

- [Python](http://www.python.org)


Installation
------------

To compile and install ccache, run these commands:

    ./autogen.sh
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
