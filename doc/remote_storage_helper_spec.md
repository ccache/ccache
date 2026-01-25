# Ccache remote storage helper specification

## Overview

      ┌────────┐
     ┌┤ ccache │     ┌────────────────┐     ┌───────────────────────┐
    ┌┤└───────┬┘<--->│ storage helper │<--->│ remote storage server │
    │└───────┬┘      └────────────────┘     └───────────────────────┘
    └────────┘

To be able to communicate with remote storage servers in an efficient way,
ccache spawns a long-lived local helper process that can keep connections alive,
thus amortizing the session setup cost. The helper process is shared by all
ccache processes that use the same remote storage server settings.

Using a separate local helper process also provides several architectural
benefits:

- Knowledge about remote storage protocols can be kept out of ccache, reducing
  its complexity and maintenance burden.
- Helper processes can be released and developed independently of ccache,
  enabling faster iteration and easier updates.
- Helper processes can be written in any programming language, allowing
  developers to choose the most suitable tools and libraries for implementing
  specific storage backends.

The storage helper is called `ccache-storage-<scheme>`, where `<scheme>` is the
scheme part of a URL, e.g. `https`. The helper acts as a client to the remote
storage server and as a server to one or several ccache clients.

The storage helper listens to a Unix-domain socket on POSIX systems and a named
pipe on Windows systems. Ccache communicates with it using a custom IPC protocol
which this document describes.

A client that has access to the socket/pipe has access to the server, so there
is no authentication or authorization mechanism within the protocol.

## Storage helper startup

The helper program will get needed information as environment variables:

- `CRSH_IPC_ENDPOINT`: Unix socket path or Windows named pipe name (without
  `\\.\pipe\` prefix).
- `CRSH_URL`: URL to the remote storage server.
- `CRSH_IDLE_TIMEOUT`: Timeout (in seconds) for the storage helper to wait
  before exiting after client inactivity. If set to 0, the helper will stay up
  indefinitely and never exit due to inactivity.

Custom attributes from ccache's `remote_storage` configuration will also
provided as environment variables:

- `CRSH_NUM_ATTR`: Number of custom attributes (key-value pairs) from ccache's
  `remote_storage` configuration.
- `CRSH_ATTR_KEY_<i>`: Custom attribute key number `i` (zero-based) from
  ccache's `remote_storage` configuration.
- `CRSH_ATTR_VALUE_<i>`: Custom attribute value number `i` (zero-based) from
  ccache's `remote_storage` configuration.

The program must try to create, bind and listen to the socket (named pipe) and
exit immediately with an error code on failure. On POSIX, use umask 077 for the
socket.

## Storage helper IPC protocol

This is a specification of the custom binary IPC protocol between ccache
("client" below) and the storage helper ("server" below).

### Notes

- Keys are at most 255 bytes long since they are typically hashes. Note that the
  key is passed in binary form, so the helper program might need to encode it
  appropriately (e.g. in hex form) when communicating with the remote storage
  server.
- Error messages are at most 255 bytes long. They must be short, descriptive
  and suitable for logging, e.g. not include a full HTML reply for an HTTP 404
  error.
- Integers are in host byte order since both client and server will always be on
  the same host.

### Primitive types

- `<byte>`: 1 byte
- `<u8>`: unsigned 8-bit integer (1 byte)
- `<u64>`: unsigned 64-bit integer in host byte order (8 bytes)

### Common types

```
<key>             ::= <key_len> <key_data>
<key_len>         ::= <u8>
<key_data>        ::= <byte>*       ; <key_len> bytes
<value>           ::= <value_len> <value_data>
<value_len>       ::= <u64>
<value_data>      ::= <byte>*       ; <value_len> bytes
<msg>             ::= <msg_len> <msg_data>
<msg_len>         ::= <u8>
<msg_data>        ::= <byte>*       ; <msg_len> bytes, UTF-8
<ok>              ::= 0x00          ; operation completed successfully
<noop>            ::= 0x01          ; operation not completed:
                                    ;   get: key not found
                                    ;   put: key/value not stored
                                    ;   remove: key/value not removed
<err>             ::= 0x02 <msg>    ; e.g. bad parameters, network/server errors
```

### Server greeting (server to client)

The server sends a greeting to the client when the client has connected. The
client must verify protocol version and server capabilities before sending any
request.

```
<greeting>        ::= <protocol_ver> <capabilities>
<protocol_ver>    ::= 0x01          ; protocol version 1
<capabilities>    ::= <cap_len> <cap_data>
<cap_len>         ::= <u8>
<cap_data>        ::= <cap>*        ; <cap_len> entries
<cap>             ::= <cap0>        ; currently only one capability is defined
<cap0>            ::= 0x00          ; get/put/remove/stop operations
```

### Requests (client to server with response from server)

#### Get

Get a value from remote storage.

```
<get_request>     ::= 0x00 <key>
<get_response>    ::= <ok> <value> | <noop> | <err>
```

#### Put

Put a value in remote storage.

```
<put_request>     ::= 0x01 <key> <put_flags> <value>
<put_flags>       ::= <u8>          ; bit 0 (LSB): overwrite, other bits ignored
<put_response>    ::= <ok> | <noop> | <err>
```

#### Remove

Remove a value from remote storage.

```
<remove_request>  ::= 0x02 <key>
<remove_response> ::= <ok> | <noop> | <err>
```

#### Stop

Tell the server to shut down and exit. The server must terminate immediately
without waiting for ongoing client operations to finish.

```
<stop_request>    ::= 0x03
<stop_response>   ::= <ok>
```

Note that the connection might be closed before `<stop_response>` can be read.
