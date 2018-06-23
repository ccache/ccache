#!/usr/bin/env python

# JSON schema for ccache manifest.json

import jsl
import json


class File(jsl.Document):
    path = jsl.StringField(required=True)
    hash = jsl.StringField(required=True)
    size = jsl.IntField(required=True)
    mtime = jsl.IntField(required=True)
    ctime = jsl.IntField(required=True)


class Object(jsl.Document):
    files = jsl.ArrayField(jsl.DocumentField(File), required=True)
    hash = jsl.StringField(required=True)
    size = jsl.IntField(required=True)


class Manifest(jsl.Document):
    version = jsl.IntField(required=True)
    hash_size = jsl.IntField(required=True)
    reserved = jsl.IntField(required=True)
    objects = jsl.ArrayField(jsl.DocumentField(Object), required=True)

if __name__ == '__main__':
    schema = Manifest.get_schema(ordered=True)
    print json.dumps(schema, indent=4).replace(" \n", "\n")
