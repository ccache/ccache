#! /usr/bin/env python
#
# Copyright (C) 2010-2019 Joel Rosdahl
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

from optparse import OptionParser
from os import access, environ, mkdir, getpid, X_OK
from os.path import (
    abspath, basename, exists, isabs, isfile, join as joinpath, realpath,
    splitext)
from shutil import rmtree
from subprocess import call
from time import time
import sys

USAGE = """%prog [options] <compiler> [compiler options] <source code file>"""

DESCRIPTION = """\
This program compiles a C/C++ file with/without ccache a number of times to get
some idea of ccache speedup and overhead in the preprocessor and direct modes.
The arguments to the program should be the compiler, optionally followed by
compiler options, and finally the source file to compile. The compiler options
must not contain -c or -o as these options will be added later. Example:
./perf.py gcc -g -O2 -Idir file.c
"""

DEFAULT_CCACHE = "./ccache"
DEFAULT_DIRECTORY = "."
DEFAULT_HIT_FACTOR = 1
DEFAULT_TIMES = 30

PHASES = [
    "without ccache",
    "with ccache, preprocessor mode, cache miss",
    "with ccache, preprocessor mode, cache hit",
    "with ccache, direct mode, cache miss",
    "with ccache, direct mode, cache hit"]

verbose = False


def progress(msg):
    if verbose:
        sys.stderr.write(msg)
        sys.stderr.flush()


def recreate_dir(x):
    if exists(x):
        rmtree(x)
    mkdir(x)


def test(tmp_dir, options, compiler_args, source_file):
    src_dir = "%s/src" % tmp_dir
    obj_dir = "%s/obj" % tmp_dir
    ccache_dir = "%s/ccache" % tmp_dir
    mkdir(src_dir)
    mkdir(obj_dir)

    compiler_args += ["-c", "-o"]
    extension = splitext(source_file)[1]
    hit_factor = options.hit_factor
    times = options.times

    progress("Creating source code\n")
    for i in range(times):
        fp = open("%s/%d%s" % (src_dir, i, extension), "w")
        fp.write(open(source_file).read())
        fp.write("\nint ccache_perf_test_%d;\n" % i)
        fp.close()

    environment = {"CCACHE_DIR": ccache_dir, "PATH": environ["PATH"]}
    environment["CCACHE_COMPILERCHECK"] = options.compilercheck
    if options.compression:
        environment["CCACHE_COMPRESS"] = "1"
    if options.hardlink:
        environment["CCACHE_HARDLINK"] = "1"
    if options.nostats:
        environment["CCACHE_NOSTATS"] = "1"

    result = [None] * len(PHASES)

    def run(i, use_ccache, use_direct):
        obj = "%s/%d.o" % (obj_dir, i)
        src = "%s/%d%s" % (src_dir, i, extension)
        if use_ccache:
            args = [options.ccache]
        else:
            args = []
        args += compiler_args + [obj, src]
        env = environment.copy()
        if not use_direct:
            env["CCACHE_NODIRECT"] = "1"
        if call(args, env=env) != 0:
            sys.stderr.write(
                'Error running "%s"; please correct\n' % " ".join(args))
            sys.exit(1)

    # Warm up the disk cache.
    recreate_dir(ccache_dir)
    recreate_dir(obj_dir)
    run(0, True, True)

    ###########################################################################
    # Without ccache
    recreate_dir(ccache_dir)
    recreate_dir(obj_dir)
    progress("Compiling %s\n" % PHASES[0])
    t0 = time()
    for i in range(times):
        run(i, False, False)
        progress(".")
    result[0] = time() - t0
    progress("\n")

    ###########################################################################
    # Preprocessor mode
    recreate_dir(ccache_dir)
    recreate_dir(obj_dir)
    progress("Compiling %s\n" % PHASES[1])
    t0 = time()
    for i in range(times):
        run(i, True, False)
        progress(".")
    result[1] = time() - t0
    progress("\n")

    recreate_dir(obj_dir)
    progress("Compiling %s\n" % PHASES[2])
    t0 = time()
    for j in range(hit_factor):
        for i in range(times):
            run(i, True, False)
            progress(".")
    result[2] = (time() - t0) / hit_factor
    progress("\n")

    ###########################################################################
    # Direct mode
    recreate_dir(ccache_dir)
    recreate_dir(obj_dir)
    progress("Compiling %s\n" % PHASES[3])
    t0 = time()
    for i in range(times):
        run(i, True, True)
        progress(".")
    result[3] = time() - t0
    progress("\n")

    recreate_dir(obj_dir)
    progress("Compiling %s\n" % PHASES[4])
    t0 = time()
    for j in range(hit_factor):
        for i in range(times):
            run(i, True, True)
            progress(".")
    result[4] = (time() - t0) / hit_factor
    progress("\n")

    return result


def print_result_as_text(result):
    for (i, x) in enumerate(PHASES):
        print "%-43s %6.2f s (%6.2f %%) (%5.2f x)" % (
            x.capitalize() + ":",
            result[i],
            100 * (result[i] / result[0]),
            result[0] / result[i])


def print_result_as_xml(result):
    print '<?xml version="1.0" encoding="UTF-8"?>'
    print "<ccache-perf>"
    for (i, x) in enumerate(PHASES):
        print "<measurement>"
        print "<name>%s</name>" % x.capitalize()
        print "<seconds>%.2f</seconds>" % result[i]
        print "<percent>%.2f</percent>" % (100 * (result[i] / result[0]))
        print "<times>%.2f</times>" % (result[0] / result[i])
        print "</measurement>"
    print "</ccache-perf>"


def on_off(x):
    return "on" if x else "off"


def find_in_path(cmd):
    if isabs(cmd):
        return cmd
    else:
        for path in environ["PATH"].split(":"):
            p = joinpath(path, cmd)
            if isfile(p) and access(p, X_OK):
                return p
        return None


def main(argv):
    op = OptionParser(usage=USAGE, description=DESCRIPTION)
    op.disable_interspersed_args()
    op.add_option(
        "--ccache",
        help="location of ccache (default: %s)" % DEFAULT_CCACHE)
    op.add_option(
        "--compilercheck",
        help="specify compilercheck (default: mtime)")
    op.add_option(
        "--compression",
        help="use compression",
        action="store_true")
    op.add_option(
        "-d", "--directory",
        help=(
            "where to create the temporary directory with the cache and other"
            " files (default: %s)" % DEFAULT_DIRECTORY))
    op.add_option(
        "--hardlink",
        help="use hard links",
        action="store_true")
    op.add_option(
        "--hit-factor",
        help=(
            "how many times more to compile the file for cache hits (default:"
            " %d)" % DEFAULT_HIT_FACTOR),
        type="int")
    op.add_option(
        "--nostats",
        help="don't write statistics",
        action="store_true")
    op.add_option(
        "-n", "--times",
        help=(
            "number of times to compile the file (default: %d)"
            % DEFAULT_TIMES),
        type="int")
    op.add_option(
        "-v", "--verbose",
        help="print progress messages",
        action="store_true")
    op.add_option(
        "--xml",
        help="print result as XML",
        action="store_true")
    op.set_defaults(
        ccache=DEFAULT_CCACHE,
        compilercheck="mtime",
        directory=DEFAULT_DIRECTORY,
        hit_factor=DEFAULT_HIT_FACTOR,
        times=DEFAULT_TIMES)
    (options, args) = op.parse_args(argv[1:])
    if len(args) < 2:
        op.error("Missing arguments; pass -h/--help for help")

    global verbose
    verbose = options.verbose

    options.ccache = abspath(options.ccache)

    compiler = find_in_path(args[0])
    if compiler is None:
        op.error("Could not find %s in PATH" % args[0])
    if "ccache" in basename(realpath(compiler)):
        op.error(
            "%s seems to be a symlink to ccache; please specify the path to"
            " the real compiler instead" % compiler)

    if not options.xml:
        print "Compilation command: %s -c -o %s.o" % (
            " ".join(args),
            splitext(argv[-1])[0])
        print "Compilercheck:", options.compilercheck
        print "Compression:", on_off(options.compression)
        print "Hardlink:", on_off(options.hardlink)
        print "Nostats:", on_off(options.nostats)

    tmp_dir = "%s/perfdir.%d" % (abspath(options.directory), getpid())
    recreate_dir(tmp_dir)
    result = test(tmp_dir, options, args[:-1], args[-1])
    rmtree(tmp_dir)
    if options.xml:
        print_result_as_xml(result)
    else:
        print_result_as_text(result)


main(sys.argv)
