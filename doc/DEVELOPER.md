Developer manual
================

Tracing
-------

In order to see what ccache is doing, it is possible to enable internal
tracing:

* Build ccache with the `--enable-tracing` configure option.
* Set the environment variable `CCACHE_INTERNAL_TRACE` to instruct ccache to
  create trace files at runtime.

There will be one trace file per ccache invocation, named as the object file
with a `.ccache-trace` suffix, e.g. `file.o.ccache-trace`. The trace file can
then be loaded into the `chrome://tracing` page of Chromium/Chrome.

You can combine several trace files into by using the `misc/combine-trace-files`
script:

    misc/combine-trace-files *.o.ccache-trace | gzip > ccache.trace.gz

(The gzip step is optional; Chrome supports both plain trace files and gzipped
trace files.) The script will offset each individual trace by its start time in
the combined file.

There is also a script called `summarize-trace-files` that generates a summary
(per job slot) of all the ccache runs:

    misc/combine-trace-files *.o.ccache-trace | misc/summarize-trace-files 4 > ccache.trace

The script takes the number of job slots you used when building (e.g. `4` for
`make -j4`) as the first argument.
