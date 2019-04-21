Developer Manual
================

Tracing
-------

In order to see what ccache is doing, it is possible to enable microsecond tracing.
This needs to be done when compiling ccache, using the `--enable-tracing` feature.

By setting *CCACHE_INTERNAL_TRACE*, one can obtain a trace of an individual compile.
This trace can then be loaded into the `chrome://tracing` page of Chromium/Chrome.

The current event categories are: config, main, hash, manifest, cache, file, execute

With a unique file per compile, there is a script to combine them all into one trace:

`misc/combine_events.py file1.json file2.json file3.json | gzip > ccache.trace.gz`

This will offset each invididual trace by starting time, to make one combined trace.

If you set the variable, the trace JSON output will be put next to the object file:

e.g. `output.o.ccache-trace`

This is done by first generating a temporary file, until the output name is known.

There is another script, to generate a summary (per job slot) of all the ccache runs:

`misc/combine_events.py *.ccache-trace | misc/summarize_events.py 1 > ccache.trace`

You will need to give the number of job slots used (`make -j`), as input to the script.
