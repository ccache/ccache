#!/usr/bin/env python3

# This script downloads the contents of the local cache
# from a Redis remote storage.

import redis
import os

import progress.bar
import progress.spinner
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

ccache = os.getenv("CCACHE_DIR", os.path.expanduser("~/.cache/ccache"))

try:
    count = context.info()["db0"]["keys"]
except Exception:
    count = None  # only for showing progress
files = result = manifest = objects = 0
size = 0
columns = os.get_terminal_size()[0]
width = min(columns - 24, 100)
bar = progress.bar.Bar(
    "Downloading...", max=count, fill="=", suffix="%(percent).1f%%", width=width
)
if not count:
    bar = progress.spinner.Spinner("Downloading... ")

# Note: doesn't work with the SSDB SCAN command
#       syntax: keys key_start, key_end, limit
for key in context.scan_iter():
    if not key.startswith(b"ccache:"):
        bar.next()
        continue
    base = key.decode().replace("ccache:", "")
    pipe.get(key)
    val = pipe.execute()[-1]
    if val is None:
        continue
    if val[0:2] == b"\xcc\xac":  # magic
        objects += 1
        if val[3] == 0:
            ext = "R"
            result += 1
        elif val[3] == 1:
            ext = "M"
            manifest += 1
        else:
            bar.next()
            continue
        filename = os.path.join(ccache, base[0], base[1], base[2:] + ext)
        if not os.path.exists(filename):
            dirname = os.path.join(ccache, base[0], base[1])
            os.makedirs(dirname, mode=0o755, exist_ok=True)
            with open(filename, "wb") as out:
                out.write(val)
    files += 1
    size += len(val)
    bar.next()
bar.finish()

print(
    "%d files, %d result (%d manifest) = %d objects (%s)"
    % (files, result, manifest, objects, humanize.naturalsize(size, binary=True))
)
