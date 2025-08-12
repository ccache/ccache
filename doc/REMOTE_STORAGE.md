# Implementing a TLV Protocol Server

This document guides server implementations that host connections using ccache's
Tag-Length-Value ([TLV](https://en.wikipedia.org/wiki/Type%E2%80%93length%E2%80%93value))
protocol, which ccache uses to send data to a mediator responsible for establishing
connections to remote storage backends and propagating ccache's requests.

## Message Structure

A TLV message consists of a header followed by a sequence of TLV fields:

```yaml
┌────────────────────────────────────────────────────────┐
│                    Message Header                      │
├────────────────────────────────────────────────────────┤
│                    TLV Field 1                         │
├────────────────────────────────────────────────────────┤
│                    TLV Field 2                         │
├────────────────────────────────────────────────────────┤
│                       ...                              │
├────────────────────────────────────────────────────────┤
│                    TLV Field N                         │
└────────────────────────────────────────────────────────┘
```

1. **Message Header (`MessageHeader`)**:
    * `version` (`uint16_t`): Indicates the protocol version.
    * `msg_type` (`uint16_t`): Specifies the type of operation or message.

2. **TLV Fields**: A sequence of fields following the TLV format.

### TLV Field Format

Each field is encoded as:

* Tag (uint8_t): Identifies the type of data within the field.
  * SETUP types: Range `0x01` to `0x80` (e.g., `SETUP_TYPE_VERSION`,
    `SETUP_TYPE_BUFFERSIZE`).
  * Application types: Range `0x81` to `0xFF` (e.g., `FIELD_TYPE_KEY`,
    `FIELD_TYPE_VALUE`).
* Length (variable): Indicates the size of the data payload.
  * 1-byte encoding: For lengths from 0 to `LENGTH_1_BYTE_MAX` (252). The
    length value is the encoded length.
  * 3-byte encoding: For lengths from 253 to `0xFFFF`. The first byte is
    `LENGTH_3_BYTE_FLAG` (253), followed by a `uint16_t` representing the length.
  * 5-byte encoding: For lengths greater than `0xFFFF`. The first byte is
    `LENGTH_5_BYTE_FLAG` (254), followed by a `uint32_t` representing the length.
  * 9-byte encoding: For lengths greater than `0xFFFFFFFF`. The first byte is
    `LENGTH_9_BYTE_FLAG` (255), followed by a `uint64_t` representing the length.
* Value: The actual data payload. Its interpretation is determined by the `Tag`
  and `Length`.

### Serialization and Deserialization Details

This section overviews how ccache serializes and parses messages. Using this procedure
is not enforced but it may provide a platform to further implementations.

* **Serialization (sending data)**:
  * The `TLVSerializer` constructs messages by first writing the `MessageHeader`
    onto a stream buffer.
  * Then, for each field, it writes the `Tag`, followed by the length (encoded
    appropriately), and then the `Value`.
  * The `addfield` overloads handle various C++ types, converting them into the
    appropriate byte representation for the `Value`.
  * The `encode_length` function implements the variable-length length encoding.

* **Deserialization (receiving data)**:
  * The `TLVParser` reads the `MessageHeader` first.
  * It then iteratively reads the `Tag`, decodes the `Length` using `decode_length`,
    and then stores a span referencing the `Value`.
  * A `parse_value` function is used for interpreting the `Value` based on the
    `Tag` and verifies whether the length of the `TagType` is correct.
  * The `getfield` function provides a way to locate specific fields within a parsed
    message. Therefore, no order of tags is enforced or would lead to benefits.

### Key Constants for Server Implementation

* `TLV_VERSION`: Protocol version.
* `TLV_HEADER_SIZE`: Size of the message header (4 bytes: `uint16_t` version +
  `uint16_t` msg\_type).
* `MAX_MSG_SIZE`: Maximum allowed message size.
* `LENGTH_i_BYTE_MAX`: Define the length encoding scheme; `i ∈ {3,5,9}`
* `SETUP_TYPE_*` and `FIELD_TYPE_*`: Tags for specific data fields.
* `MSG_TYPE_*`: Identifiers for different message types (requests and responses).
* `ResponseStatus`: Enum for status codes returned in response messages.
  * `LOCAL_ERROR`:  0x01
  * `NO_FILE`:      0x02
  * `TIMEOUT`:      0x03
  * `SIGWAIT`:      0x04
  * `SUCCESS`:      0x05
  * `REDIRECT`:     0x06
  * `ERROR`:        0x07

### Message Details

#### SETUP Request

* **Message Type**: `MSG_TYPE_SETUP_REQUEST` (0x01)
* **Required Fields**:
  * `SETUP_TYPE_VERSION`: MUST include protocol version
  * `SETUP_TYPE_BUFFERSIZE`: MUST specify buffer size
* **Optional Fields**:
  * `SETUP_TYPE_TIMEOUT`: MAY specify a timeout (default: 5s)
  * `SETUP_TYPE_FD_MODE`: MAY indicate the possibility to pass a fd (default: false)

#### SETUP Response

* **Message Type**: `MSG_TYPE_SETUP_RESPONSE` (0x8001)
* **Required Fields**:
  * `FIELD_TYPE_STATUS_CODE`: MUST contain the status of response
* **Optional Fields**:
  * `SETUP_TYPE_TIMEOUT`: MAY specify a timeout (default: 5s)
  * `SETUP_TYPE_VERSION`: MAY specify protocol version
  * `SETUP_TYPE_BUFFERSIZE`: MAY specify buffer size
  * `SETUP_TYPE_FD_MODE`: MAY indicate the possibility to pass a fd (default: false)
  * `FIELD_TYPE_ERROR_MSG`: SHOULD contain an error message if failed

#### GET Request

* **Message Type**: `MSG_TYPE_GET_REQUEST` (0x02)
* **Required Fields**:
  * `FIELD_TYPE_KEY`: MUST contain the key to retrieve
* **Optional Fields**: None

#### GET Response

* **Message Type**: `MSG_TYPE_GET_RESPONSE` (0x8002)
* **Required Fields**:
  * `FIELD_TYPE_STATUS_CODE`: MUST contain the status of response
* **Optional Fields**:
  * `FIELD_TYPE_VALUE`: MUST NOT contain data if the request fails or no file
  * `FIELD_TYPE_ERROR_MSG`: SHOULD contain an error message if failed

#### PUT Request

* **Message Type**: `MSG_TYPE_PUT_REQUEST` (0x03)
* **Required Fields**:
  * `FIELD_TYPE_KEY`: MUST contain the key to store
  * `FIELD_TYPE_VALUE`: MUST contain the value data
* **Optional Fields**:
  * `FIELD_TYPE_FLAGS`: MAY indicate overwrite permission
  * `FIELD_TYPE_FD`: MAY contain fd if negotiated but then value would be empty

#### PUT Response

* **Message Type**: `MSG_TYPE_PUT_RESPONSE` (0x8003)
* **Required Fields**:
  * `FIELD_TYPE_STATUS_CODE`: MUST contain the status of response
* **Optional Fields**:
  * `FIELD_TYPE_ERROR_MSG`: SHOULD contain an error message if failed

#### REMOVE Request

* **Message Type**: `MSG_TYPE_DEL_REQUEST` (0x04)
* **Required Fields**:
  * `FIELD_TYPE_KEY`: MUST contain the key to remove
* **Optional Fields**: None

#### REMOVE Response

* **Message Type**: `MSG_TYPE_DEL_RESPONSE` (0x8004)
* **Required Fields**:
  * `FIELD_TYPE_STATUS_CODE`: MUST contain the status of response
* **Optional Fields**:
  * `FIELD_TYPE_ERROR_MSG`: SHOULD contain an error message if failed

## Server Implemenation and Build

A server implementing this TLV protocol would need an executable in `libexec`
directory. On Windows, the executable should be located somewhere in the `Program
Files`

```shell
# Linux paths to search
/usr/local/libexec/ccache
/usr/libexec/ccache
# Windows paths to search
C:\\Program Files\\ccache\\ (64-bit)
C:\\Program Files\ (x86)\\ccache\\ (32-bit)
```

The following points have to be considered.

1. **Initialization**:
    * Bind a connection over the socket passed by ccache.
    * Receive a setup message to start handshake. (See setup types)
    * A handshake involves the initial request from ccache and a single `SETUP_RESPONSE`,
      which includes what the server agrees on and supports.
    * If the setup is compatible, ccache begins sending transactions. Otherwise, ccache
      ends the connection and falls back to local storage only.

2. **Network Listening**:
    * Establish a network listener over a Unix domain socket to accept incoming client
      connections.
    * The Unix socket path is supplied by ccache via environment variables when ccache
      launches the backend.

3. **Connection Handling**:
    * On accepting a new client connection:
        * Associate a per-connection communication buffer.
        * Enter a loop to continuously read data from the client.
    * Favor a zero-copy approach on the mediator's network stack to minimize overhead.
    * After sending a request, ccache waits for a message and does not rely on a delimiter.
    * A thin wrapper around a POSIX select call with a 100 ms timeout is used to detect
      when the peer is done with sending data.
    * Note: ccache will wait up to 15 seconds for a complete file to become available.
