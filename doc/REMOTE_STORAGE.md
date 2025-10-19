# ccache Remote Storage TLV Protocol Specification

## 1. Introduction

This document defines the Type-Length-Value (TLV) protocol used for communication between the `ccache` utility and its remote storage backend processes. This protocol enables `ccache` to offload storage operations (like get, put, delete) to a separate server process that manages connections to various remote storage systems.

## 2. Protocol Basics

### 2.1. Message Structure

All messages exchanged between `ccache` (the client) and the remote storage backend (the server) adhere to a TLV-based structure. A message consists of a fixed `MessageHeader` followed by a sequence of TLV-encoded fields.

```html
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

### 2.2. Byte Order and Endianness

All multi-byte numeric values within the protocol (e.g., `version`, `msg_type`, lengths) MUST be encoded in **host byte order**.

### 2.3. Length Encoding

Field lengths are encoded using a variable-length scheme to accommodate a wide range of data sizes efficiently.

* **1-byte encoding:** For lengths from `0` to `252`. The length value is the encoded length itself.
* **3-byte encoding:** For lengths from `253` to `65,535` (`0xFFFF`). The first byte is `0xFD` (`LENGTH_3_BYTE_FLAG`), followed by a `uint16_t` representing the length.
* **5-byte encoding:** For lengths from `65,536` to `4,294,967,295` (`0xFFFFFFFF`). The first byte is `0xFE` (`LENGTH_5_BYTE_FLAG`), followed by a `uint32_t` representing the length.
* **9-byte encoding:** For lengths greater than `4,294,967,295`. The first byte is `0xFF` (`LENGTH_9_BYTE_FLAG`), followed by a `uint64_t` representing the length.

## 3. Message Header

The `MessageHeader` provides fundamental information about the message, including the number of TLV fields it contains.

* **Structure:**

    ```c++
    struct __attribute__((packed)) MessageHeader {
        uint8_t version;      // Protocol version
        uint8_t field_count;  // Number of TLV fields in this message
        uint16_t msg_type;    // Type of message
    };
    ```

* **Size:** `TLV_HEADER_SIZE` is `4` bytes (`uint8_t` version + `uint8_t` field_count + `uint16_t` msg_type).

## 4. TLV Field Format

Each TLV field consists of a Tag, a Length, and a Value.

```html
┌───────────┬───────────────┬───────────────────────────┐
│  Tag      │    Length     │           Value           │
│ (uint8_t) │ (variable)    │      (Length bytes)       │
└───────────┴───────────────┴───────────────────────────┘
```

* **Tag (`uint8_t`):** Identifies the type of data in the `Value` field.
  * **SETUP types:** `0x01` to `0x80`
  * **Application types:** `0x81` to `0xFF`
* **Length (`variable`):** Encodes the size of the `Value` field as described in Section 2.3.
* **Value:** The actual data payload, interpreted based on the `Tag`. The `Length` field dictates the number of bytes to read for the `Value`.

## 5. Field and Message Type Definitions

### 5.1. Tags

Tags are categorized into SETUP types and Application types.

#### 5.1.1. SETUP Types (`0x01` - `0x80`)

| Tag Name             | Value      | Data Type | Description                                                  | Required/Optional (Context Dependent) |
| :------------------- | :--------- | :-------- | :----------------------------------------------------------- | :------------------------------------ |
| `SETUP_TYPE_VERSION` | `0x01`     | `uint16_t`| Protocol version number.                                     | REQUIRED for `SETUP_REQUEST`            |
| `SETUP_TYPE_OPERATION_TIMEOUT` | `0x02`     | `uint32_t`| Timeout in milliseconds. See Section 6.1 for interpretation. | OPTIONAL                              |
| `SETUP_TYPE_BUFFERSIZE`| `0x03`     | `uint32_t`| Buffer size in bytes. See Section 6.2 for interpretation.    | OPTIONAL            |

#### 5.1.2. Application Types (`0x81` - `0xFF`)

| Tag Name             | Value      | Data Type | Description                                           | Required/Optional (Context Dependent) |
| :------------------- | :--------- | :-------- | :---------------------------------------------------- | :------------------------------------ |
| `FIELD_TYPE_KEY`     | `0x81`     | `bytes`  | The key for storage operations (e.g., cache object hash). | REQUIRED for GET, PUT, DEL            |
| `FIELD_TYPE_VALUE`   | `0x82`     | `bytes`   | The data payload for storage operations.              | REQUIRED for PUT; OPTIONAL for GET    |
| `FIELD_TYPE_TIMESTAMP`| `0x83`     | `uint64_t`| Unix timestamp. (Currently unused in core operations). | OPTIONAL                              |
| `FIELD_TYPE_STATUS_CODE`| `0x84`     | `ResponseStatus` | Status code of the operation.                         | REQUIRED for responses                |
| `FIELD_TYPE_ERROR_MSG`| `0x85`     | `string`  | Detailed error message.                               | MUST NOT on success            |
| `FIELD_TYPE_FLAGS`   | `0x86`     | `uint8_t` | Flags for operations (e.g., overwrite).               | OPTIONAL                              |

### 5.2. Message Types

Message types are `uint16_t`. Request types are in the `0x01`-`0x8000` range, and their corresponding responses are in the `0x8001`-`0x800F` range.

| Message Type Name           | Value   | Direction | Description                                       |
| :-------------------------- | :------ | :-------- | :------------------------------------------------ |
| `MSG_TYPE_SETUP_REQUEST`    | `0x01`  | Client->S | Initiates the handshake and configuration exchange. |
| `MSG_TYPE_SETUP_RESPONSE`   | `0x8001`| Server->C | Server's response to the setup request.           |
| `MSG_TYPE_GET_REQUEST`      | `0x02`  | Client->S | Request to retrieve a value by its key.           |
| `MSG_TYPE_GET_RESPONSE`     | `0x8002`| Server->C | Response to a GET request.                        |
| `MSG_TYPE_PUT_REQUEST`      | `0x03`  | Client->S | Request to store a key-value pair.                |
| `MSG_TYPE_PUT_RESPONSE`     | `0x8003`| Server->C | Response to a PUT request.                        |
| `MSG_TYPE_DEL_REQUEST`      | `0x04`  | Client->S | Request to remove a key.                          |
| `MSG_TYPE_DEL_RESPONSE`     | `0x8004`| Server->C | Response to a DELETE request.                     |

### 5.3. Constants and Enums

* `TLV_HEADER_SIZE`: `4` bytes.
* `TLV_MAX_FIELD_SIZE`: No explicit maximum size is enforced by the protocol definition, but implementations should be mindful of system limits. Length encoding supports up to `2^64` bytes.
* `LENGTH_1_BYTE_MAX` (implicitly `252`), `LENGTH_3_BYTE_FLAG` (`0xFD`), `LENGTH_5_BYTE_FLAG` (`0xFE`), `LENGTH_9_BYTE_FLAG` (`0xFF`).
* `OVERWRITE_FLAG` (`0x01`): Used with `FIELD_TYPE_FLAGS` to indicate overwrite permission for PUT operations.

#### 5.3.1. `ResponseStatus` Enum

Used within `FIELD_TYPE_STATUS_CODE`.

| Status Code       | Value     | Description                                          |
| :---------------- | :-------- | :--------------------------------------------------- |
| `SUCCESS`         | `0x00`    | Operation completed successfully.                    |
| `NO_FILE`         | `0x01`    | Key not found.                                       |
| `TIMEOUT`         | `0x02`    | Operation timed out.                                 |
| `LOCAL_ERROR`     | `0x03`    | Error on the local (client) side.                    |
| `ERROR`           | `0x04`    | A general error occurred.                            |

## 6. Message Details and Semantics

### 6.1. Timeouts

The `SETUP_TYPE_OPERATION_TIMEOUT` tag can be used to convey timeout information. The interpretation depends on the context:

* **Client to Server:** The client MAY send `SETUP_TYPE_OPERATION_TIMEOUT` to suggest a timeout for **operation timeout**. This specifies how long the backend process should wait before it times out on an operation.
* **Server to Client:** The server MAY send `SETUP_TYPE_OPERATION_TIMEOUT` in `SETUP_RESPONSE` to specify its configured **operation timeout**. It MAY also suggest a different **operation timeout**.

The client is responsible for managing its own "Connection timeout" (waiting for initial server reply) and "Operation timeout" (aborting an operation if it takes too long).

### 6.2. Buffer Size

The `SETUP_TYPE_BUFFERSIZE` tag is used to convey the preferred buffer size for communication, primarily relevant for the underlying socket operations. The client sends its preferred size, and the server responds with its actual configured buffer size.

### 6.3. Message Definitions

#### 6.3.1. `SETUP` Request/Response

* **`MSG_TYPE_SETUP_REQUEST` (Client -> Server):**
  * **Required:**
    * `SETUP_TYPE_VERSION`: Client's protocol version.
    * `SETUP_TYPE_BUFFERSIZE`: Client's preferred buffer size.
  * **Optional:**
    * `SETUP_TYPE_OPERATION_TIMEOUT`: Client's suggested operation timeout.

* **`MSG_TYPE_SETUP_RESPONSE` (Server -> Client):**
  * **Required:**
    * `FIELD_TYPE_STATUS_CODE`: Server's response status.
  * **Optional:**
    * `SETUP_TYPE_VERSION`: Server's supported protocol version.
    * `SETUP_TYPE_BUFFERSIZE`: Server's configured buffer size.
    * `SETUP_TYPE_OPERATION_TIMEOUT`: Server's configured operation timeout.
    * `FIELD_TYPE_ERROR_MSG`: Server's error message if `FIELD_TYPE_STATUS_CODE` indicates failure.

#### 6.3.2. `GET` Request/Response

* **`MSG_TYPE_GET_REQUEST` (Client -> Server):**
  * **Required:**
    * `FIELD_TYPE_KEY`: The key to retrieve.
  * **Optional:** None.

* **`MSG_TYPE_GET_RESPONSE` (Server -> Client):**
  * **Required:**
    * `FIELD_TYPE_STATUS_CODE`: Operation status.
  * **Optional:**
    * `FIELD_TYPE_VALUE`: The retrieved data. MUST NOT be present if status is not `SUCCESS` or `NO_FILE`.
    * `FIELD_TYPE_ERROR_MSG`: Error message if status is not `SUCCESS` or `NO_FILE`.

#### 6.3.3. `PUT` Request/Response

* **`MSG_TYPE_PUT_REQUEST` (Client -> Server):**
  * **Required:**
    * `FIELD_TYPE_KEY`: The key to store.
    * `FIELD_TYPE_VALUE`: The data to store.
  * **Optional:**
    * `FIELD_TYPE_FLAGS`: Flags like `OVERWRITE_FLAG`.

* **`MSG_TYPE_PUT_RESPONSE` (Server -> Client):**
  * **Required:**
    * `FIELD_TYPE_STATUS_CODE`: Operation status.
  * **Optional:**
    * `FIELD_TYPE_ERROR_MSG`: Error message if status is not `SUCCESS`.

#### 6.3.4. `DELETE` Request/Response

* **`MSG_TYPE_DEL_REQUEST` (Client -> Server):**
  * **Required:**
    * `FIELD_TYPE_KEY`: The key to remove.
  * **Optional:** None.

* **`MSG_TYPE_DEL_RESPONSE` (Server -> Client):**
  * **Required:**
    * `FIELD_TYPE_STATUS_CODE`: Operation status.
  * **Optional:**
    * `FIELD_TYPE_ERROR_MSG`: Error message if status is not `SUCCESS`.

## 7. Attribute and Credential Handling

### 7.1. Passing Configuration

`ccache` parses configuration items of a provided URL and passes it to the client.

* **Remote URL:** Passed via **environment variables** to the backend process as `_CCACHE_REMOTE_URL`.
* **Socket Path:** Passed as `_CCACHE_SOCKET_PATH`.
* **Buffer size:** Propagated here as `_CCACHE_BUFFER_SIZE` too, such that it becomes less necessary to communicate it within the protocol.
* **Attributes:**: Client should read `_CCACHE_NUM_ATTR` first before accessing the attributes; each is defined as a pair (`_CCACHE_ATTR_KEY_i`, `_CCACHE_ATTR_VALUE_i`) for $0\leq i <$ `_CCACHE_NUM_ATTR`.
<!-- TODO * **Configuration File Paths:** For backends that require specific configuration files (e.g., GCP credentials), paths can be passed as attributes. The backend process is responsible for interpreting these. -->

The backend process is responsible for parsing and acting upon these attributes and credentials.

### 7.2. Capabilities Negotiation

The SETUP phase is intended for negotiation. In the current implementation, the server waits for a setup request in which the client specifies the desired configurations. The server may accept the request as-is and respond with a SUCCESS status, in which case no further negotiation is required. If the server does not support the requested setup, it may propose alternative configurations. The client checks whether these configurations are within its capabilities; if so, it proceeds with the connection, otherwise backend storage will be disabled. This model reduces the setup to two messages and one round-trip time (RTT).
