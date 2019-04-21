Developer manual
================

Tracing
-------

In order to see what ccache is doing, it is possible to enable microsecond
tracing. This needs to be done when compiling ccache using the
`--enable-tracing` configure option.

By setting `CCACHE_INTERNAL_TRACE` one can obtain a trace of an individual
compile. This trace can then be loaded into the `chrome://tracing` page of
Chromium/Chrome.

The current event categories are config, main, hash, manifest, cache, file,
execute.

There is a script to combine trace logs from multiple compilations into one:

    misc/combine_events.py file_1.json ... file_n.json | gzip > ccache.trace.gz

This will offset each invididual trace by starting time to make one combined
trace.

When you set the `CCACHE_INTERNAL_TRACE` variable, the trace JSON output will
be put next to the object file, e.g. as `output.o.ccache-trace`. This is done
by first generating a temporary file until the output name is known.

There is also another script to generate a summary (per job slot) of all the
ccache runs:

    misc/combine_events.py *.ccache-trace | misc/summarize_events.py 1 > ccache.trace

You will need to give the number of job slots used (`make -j`) as input to the
script.
