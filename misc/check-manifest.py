#!/usr/bin/env python

import sys
import os
import json
import hashlib

try:
    md4 = hashlib.md4
except AttributeError:
    from Crypto.Hash.MD4 import MD4Hash as md4
except ImportError:
    raise Error("please install pycrypto")


def hash(file):
    return md4(file.read()).hexdigest()

mf = json.load(open(sys.argv[1]))
for object in mf['objects']:
    print "%s-%d" % (object['hash'], object['size'])
    ok = True
    sloppy = False
    for file in object['files']:
        path = file['path']
        if os.path.exists(path):
            st = os.stat(path)
            if st.st_size != file['size']:
                status = 'S'
                reason = " (%d != %d)" % (st.st_size, file['size'])
            elif sloppy and int(st.st_mtime) != file['mtime']:
                status = 'M'
                reason = " (%d != %d)" % (st.st_mtime, file['mtime'])
            elif sloppy and int(st.st_ctime) != file['ctime']:
                status = 'C'
                reason = " (%d != %d)" % (st.st_ctime, file['ctime'])
            elif hash(open(path)) != file['hash']:
                status = 'H'
                reason = " (%s != %s)" % (hash(open(path)), file['hash'])
            else:
                status = '-'
                reason = ""
        else:
            status = '?'
            reason = " (file not found)"
        print "  %c %s%s" % (status, path, reason)
        if status != '-':
            ok = False
    if ok:
        print "  = PASS"
    else:
        print "  = FAIL"
