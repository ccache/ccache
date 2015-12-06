#!/usr/bin/env python

import memcache
import struct
import os
import hashlib

"""
/* blob format for storing:

    char magic[4]; # 'CCH1', might change for other version of ccache
                   # ccache will erase the blob in memcached if wrong magic
    uint32_t obj_len; # network endian
    char *obj[obj_len];
    uint32_t stderr_len; # network endian
    char *stderr[stderr_len];
    uint32_t dia_len; # network endian
    char *dia[dia_len];
    uint32_t dep_len; # network endian
    char *dep[dep_len];

*/
"""
MEMCCACHE_MAGIC = 'CCH1'

def set_blob(data):
    return struct.pack('!I', len(data)) + str(data)
MEMCCACHE_BIG = 'CCBM'

"""
/* blob format for big values:

    char magic[4]; # 'CCBM'
    uint32_t numkeys; # network endian
    uint32_t hash_size; # network endian
    uint32_t reserved; # network endian
    uint32_t value_length; # network endian

    <hash[0]>       hash of include file                (<hash_size> bytes)
    <size[0]>       size of include file                (4 bytes unsigned int)
    ...
    <hash[n-1]>
    <size[n-1]>

*/
"""
MEMCCACHE_BIG = 'CCBM'

MAX_VALUE_SIZE = 1000 << 10 # 1M with memcached overhead
SPLIT_VALUE_SIZE = MAX_VALUE_SIZE

server = os.getenv("MEMCACHED_SERVERS", "localhost")
mc = memcache.Client([server], debug=1)

ccache = os.getenv("CCACHE_DIR", os.path.expanduser("~/.ccache"))
filelist = []
for dirpath, dirnames, filenames in os.walk(ccache):
    # sort by modification time, most recently used last
    for filename in filenames:
        stat = os.stat(os.path.join(dirpath, filename))
        filelist.append((stat.st_mtime, dirpath, filename))
filelist.sort()
files = blobs = chunks = objects = manifest = 0
for mtime, dirpath, filename in filelist:
    dirname = dirpath.replace(ccache + os.path.sep, "")
    if filename == "CACHEDIR.TAG":
        # ignore these
        files = files + 1
    else:
        (base, ext) = os.path.splitext(filename)
        if ext == '.o':
            objects = objects + 1
            key = "".join(list(os.path.split(dirname)) + [base])
            def read_file(path):
                return os.path.exists(path) and open(path).read() or ""
            obj = read_file(os.path.join(dirpath, filename))
            stderr = read_file(os.path.join(dirpath, base) + '.stderr')
            dia = read_file(os.path.join(dirpath, base) + '.dia')
            dep = read_file(os.path.join(dirpath, base) + '.d')

            print "%s: %d %d %d %d" % (key, len(obj), len(stderr), len(dia), len(dep))
            val = MEMCCACHE_MAGIC
            val += set_blob(obj)
            val += set_blob(stderr)
            val += set_blob(dia)
            val += set_blob(dep)
            if len(val) > MAX_VALUE_SIZE:
                numkeys = (len(val) + SPLIT_VALUE_SIZE - 1) / SPLIT_VALUE_SIZE
                buf = MEMCCACHE_BIG
                buf += struct.pack('!I', numkeys)
                buf += struct.pack('!I', 16)
                buf += struct.pack('!I', 0)
                buf += struct.pack('!I', len(val))
                def splitchunks(s, n):
                    """Produce `n`-character chunks from `s`."""
                    for start in range(0, len(s), n):
                        yield s[start:start+n]
                valmap = {}
                for subval in splitchunks(val, SPLIT_VALUE_SIZE):
                    subhash = hashlib.new('md4')
                    subhash.update(subval)
                    buf += subhash.digest() + struct.pack('!I', len(subval))
                    subkey = "%s-%d" % (subhash.hexdigest(), len(subval))
                    print "# %s: chunk %d" % (subkey, len(subval))
                    #mc.set(subkey, subval)
                    valmap[subkey] = subval
                    chunks = chunks + 1
                mc.set_multi(valmap)
                mc.set(key, buf)
            else:
                mc.set(key, val)
            files = files + 1
            blobs = blobs + 1
        elif ext == '.stderr' or ext == '.d' or ext == '.dia':
            # was added above
            files = files + 1
        elif ext == '.manifest':
            manifest = manifest + 1
            key = "".join(list(os.path.split(dirname)) + [base])
            val = open(os.path.join(dirpath, filename)).read() or None
            if val:
                print "%s: manifest %d" % (key, len(val))
                mc.set(key, val, 0, 0)
            files = files + 1
            blobs = blobs + 1
print "%d files, %d objects (%d manifest) = %d blobs (%d chunks)" % (files, objects, manifest, blobs, chunks)
