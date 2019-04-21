#!/usr/bin/env python

import sys
import json

trace = json.load(sys.stdin)

events = trace["traceEvents"]
slotevents = []

events.sort(key = lambda event: event["ts"])

jobs = int(sys.argv[1])

pids = {}
busy = [None] * jobs

def find_slot(pid):
    if pid in pids:
        return pids[pid]
    for slot in range(jobs):
        if not busy[slot]:
            busy[slot] = pid
            pids[pid] = slot
            return slot
    return None

def end_slot(pid):
    for slot in range(jobs):
        if busy[slot] == pid:
            busy[slot] = None
            del pids[pid]
            return slot
    return slot

name = {}
slot = -1
for event in events:
    cat = event['cat']
    pid = event['pid']
    phase = event['ph']
    args = event['args']

    if phase == 'M' and event['name'] == "thread_name":
        name[pid] = args["name"]
    if cat != "program":
        continue

    if phase == 'B' or phase == 'S':
        slot = find_slot(pid)
    elif phase == 'E' or phase == 'F':
        slot = end_slot(pid)
    elif phase == 'M':
        pass
    else:
        continue

    event['pid'] = slot
    event['tid'] = pid

    slotevents.append(event)

slotevents.sort(key = lambda event: event["tid"])

for event in slotevents:
    if event['cat'] == "program":
        event['cat'] = "ccache"
        if event['tid'] in name:
            event['name'] = name[event['tid']]
    else:
        if event['tid'] in name:
            event['name'] = event['name'] + ':' + name[event['tid']]
    del event['tid']
    if event['ph'] == 'S':
        event['ph'] = 'B'
    if event['ph'] == 'F':
        event['ph'] = 'E'

for slot in range(jobs):
    slotevents.append({"cat":"","pid":slot,"tid":0,"ph":"M","name":"process_name","args":{"name":"Job %d" % slot}})

json.dump({"traceEvents": slotevents}, sys.stdout, indent=4)
