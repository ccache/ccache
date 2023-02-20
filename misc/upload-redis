#!/usr/bin/env python3

# This script uploads the contents of the local cache
# to a Redis remote storage.

import redis
import os

import progress.bar
import humanize

config = os.getenv("REDIS_CONF", "localhost")
if ":" in config:
    host, port = config.rsplit(":", 1)
    sock = None
elif config.startswith("/"):
    host, port, sock = None, None, config
else:
    host, port, sock = config, 6379, None

username = os.getenv("REDIS_USERNAME")
password = os.getenv("REDIS_PASSWORD")
context = redis.Redis(host=host, port=port, unix_socket_path=sock, password=password)
pipe = context.pipeline(transaction=False)

use_setnx = True

ccache = os.getenv("CCACHE_DIR", os.path.expanduser("~/.cache/ccache"))
filelist = []
for dirpath, dirnames, filenames in os.walk(ccache, topdown=True):
    # sort by modification time, most recently used last
    for filename in filenames:
        if filename.endswith(".lock"):
            continue
        stat = os.stat(os.path.join(dirpath, filename))
        filelist.append((stat.st_mtime, dirpath, filename, stat.st_size))
    dirnames[:] = [d for d in dirnames if d != "tmp"]
filelist.sort()

files = result = manifest = objects = 0
size = 0
batchsize = 0
columns = os.get_terminal_size()[0]
width = min(columns - 22, 100)
bar = progress.bar.Bar(
    "Uploading...", max=len(filelist), fill="=", suffix="%(percent).1f%%", width=width
)
for mtime, dirpath, filename, filesize in filelist:
    dirname = dirpath.replace(ccache + os.path.sep, "")
    if filename != "ccache.conf" and filename != "CACHEDIR.TAG" and filename != "stats":
        (base, ext) = filename[:-1], filename[-1:]
        if ext == "R" or ext == "M":
            if ext == "R":
                result += 1
            if ext == "M":
                manifest += 1
            key = "ccache:" + "".join(list(os.path.split(dirname)) + [base])
            val = open(os.path.join(dirpath, filename), "rb").read() or None
            if val:
                # print("%s: %s %d" % (key, ext, len(val)))
                if use_setnx:
                    pipe.setnx(key, val)
                else:
                    pipe.set(key, val)
                objects += 1
        files += 1
        size += filesize
        batchsize += filesize
        if batchsize > 64 * 1024 * 1024:
            pipe.execute()
            batchsize = 0
    bar.next()
pipe.execute()
bar.finish()

print(
    "%d files, %d result (%d manifest) = %d objects (%s)"
    % (files, result, manifest, objects, humanize.naturalsize(size, binary=True))
)
