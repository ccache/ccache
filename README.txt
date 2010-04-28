ccache README
=============


About
-----

ccache is a compiler cache. It speeds up recompilation of C/C++ code by caching
previous compiles and detecting when the same compile is being done again. The
main focus is to handle the GNU C/C++ compiler (GCC), but it may also work with
compilers that mimic GCC good enough.

Please see the manual page and documentation at http://ccache.samba.org for
more information.


Documentation
-------------

See the ccache(1) man page. It's also avaiable as manual.txt and manual.html.


Installation
------------

See INSTALL.txt or INSTALL.html.


Web site
--------

The main ccache web site is here:

    http://ccache.samba.org


Mailing list
------------

There is a mailing list for discussing usage and development of ccache:

    http://lists.samba.org/mailman/listinfo/ccache/

Anyone is welcome to join.


Bug reports
-----------

To submit a bug report or to search for existing reports, please visit this web
page:

    http://ccache.samba.org/bugs.html


Source code repository
----------------------

To get the very latest version of ccache directly from the source code
repository, use git:

    git clone git://git.samba.org/ccache.git

You can also browse the repository:

    http://gitweb.samba.org/?p=ccache.git


History
-------

ccache was originally written by Andrew Tridgell and is currently maintained by
Joel Rosdahl. ccache started out as a reimplementation of Erik Thiele's
``compilercache'' (see http://www.erikyyy.de/compilercache/) in C.

See also the NEWS file.


Copyright
---------

Copyright (C) 2002-2007 Andrew Tridgell
Copyright (C) 2009-2010 Joel Rosdahl

ccache may be used, modified and redistributed only under the terms of the GNU
General Public License version 3 or later, found in the file COPYING in this
distribution, or on this web page:

    http://www.fsf.org/licenses/gpl.html
