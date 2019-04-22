#!/usr/bin/env python

import sys
import json

traces = {}
for arg in sys.argv[1:]:
    object = json.load(open(arg))
    for event in object["traceEvents"]:
        if event["name"] == "" and event["ph"] == "I":
            time = float(event["args"]["time"])
            # print "%.6f" % time
            traces[time] = object
            break

times = traces.keys()
mintime = min(times)
times.sort()

events = []
for time in times:
    offset = (time - mintime) * 1000000.0
    object = traces[time]
    for event in object["traceEvents"]:
        event["ts"] = int(event["ts"] + offset)
        events.append(event)

print json.dumps({"traceEvents": events})
