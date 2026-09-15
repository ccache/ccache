# OCI/GHCR and GitHub Actions remote storage

This is the single source of truth for the `oci://` and `gha://` remote
storage backends. It separates code, local checks and external acceptance.
An item is not externally validated unless this document says so explicitly.

## Status

| Area | Code | Local evidence | External evidence |
| --- | --- | --- | --- |
| Full digest keys and prefixes | Implemented | Unit tests | Not required |
| GHA v1 read/write | Implemented | Unit configuration tests | Not run against GitHub |
| GHA v2 read/write | Implemented | Local mock test is Linux-only | Not run against GitHub |
| GHA no credentials | Implemented | Windows compile: exit 0, `CCACHE_NG-ERROR-GHA-0002`, 0 requests | Not run in GitHub |
| GHA persistent write cooldown | Implemented | Unit test; Linux mock test added | Linux mock test not run here |
| OCI blob and manifest read/write/delete | Implemented | Unit tests | Not run against GHCR |
| Redis, OCI and backfill | Existing generic storage implementation | Linux integration test exists | Not run here |
| Docker credential helper | Implemented | JSON and configuration unit tests | Not run with a real helper or registry |
| systemd credential | Implemented on Linux | Linux unit test | Not run in a systemd service |
| Manual GitHub workflow | Present, manual dispatch only | Bash syntax and credential preflight | No run ID |

The GitHub Actions run ID list is empty. No GitHub workflow, cache UI or REST
cache list was accessed while preparing this document. No external GHCR entry
was created, read or deleted.

## Feature matrix

| Feature | `gha://` | `oci://` |
| --- | --- | --- |
| Default transport | GitHub-provided HTTPS endpoint | HTTPS |
| Read | v1 and v2 cache APIs | OCI manifest, then cache blob |
| Write | v1 and v2 reserve, upload and finalize | Blob upload, then manifest publish |
| Delete | Not supported; returns `CCACHE_NG-ERROR-GHA-0009` | Resolves manifest digest and deletes the manifest |
| Cache key | Full ccache digest, lower-case hexadecimal | Full ccache digest, lower-case hexadecimal |
| Prefix | URL path or `@prefix` | `/ccache/PREFIX` URL part or `@prefix` |
| Read-only | Generic `read-only` storage option | Generic `read-only` storage option |
| Debug | Status, operation and redacted URL | Status, operation and redacted URL |
| Credentials | Runtime values supplied by GitHub Actions | Docker helper, private file or Linux systemd credential |

The digest is not truncated. The implementation does not add repository,
branch, hostname, user name or address information to it.

## Configuration

`remote_storage` is ordered from fastest to slowest backend. A typical order
is Redis, OCI/GHCR, then GHA:

```ini
remote_storage = redis://cache.example.invalid:6379 oci://ghcr.io/OWNER/REPOSITORY/ccache/team @credential-helper=pass gha://team @service-version=v2 @debug=false
```

The example contains no secret. `pass` is only the Docker credential-helper
name; ccache invokes `docker-credential-pass get` and sends the registry name
on standard input. The helper response is read in memory and its `Secret`
field is never logged.

OCI attributes:

| Attribute | Meaning |
| --- | --- |
| `@credential-helper=NAME` | Docker credential helper name. Only letters, digits, `_` and `-` are accepted. |
| `@credential-file=PATH` | Private credential file. On Unix it must be a regular file owned by the effective user or root and have no group or other permissions. |
| `@credential-file-env=NAME` | Environment variable containing only the path to a private credential file. |
| `@systemd-credential=NAME` | Linux only. Reads `NAME` below `CREDENTIALS_DIRECTORY`; path separators are rejected. |
| `@prefix=VALUE` | Explicit cache-key prefix. |
| `@insecure=true` | Uses HTTP. Intended only for an isolated local registry test. |
| `@debug=true` | Enables redacted diagnostics. |
| `@connect-timeout=TIME` and `@operation-timeout=TIME` | Transport timeouts. |

Exactly one OCI credential source may be configured. The former OCI
`@token`, `@token-env`, `@token-file` and `@token-file-env` attributes are
rejected with `CCACHE_NG-ERROR-OCI-0037`.

GHA attributes:

| Attribute | Meaning |
| --- | --- |
| `@url=URL` or `@url-env=NAME` | Explicit endpoint for an isolated test. Production runners normally provide the endpoint. |
| `@prefix=VALUE` | Explicit cache-key prefix. |
| `@service-version=v1` or `v2` | Selects the API version. The runtime v2 marker selects v2 by default. |
| `@debug=true` | Enables redacted diagnostics. |
| `@connect-timeout=TIME` and `@operation-timeout=TIME` | Transport timeouts. |

GHA credentials are read only from `ACTIONS_RUNTIME_TOKEN`, with
`ACTIONS_ID_TOKEN_REQUEST_TOKEN` as the existing fallback. `@token` and
`@token-env` are rejected with `CCACHE_NG-ERROR-GHA-0021`. Missing endpoint or
runtime credential prevents backend construction before an HTTP request and
uses `CCACHE_NG-ERROR-GHA-0001` or `CCACHE_NG-ERROR-GHA-0002`.

## Protocol flows

### OCI

Read:

1. Get the OCI manifest for the full-digest tag.
2. Read the cache-layer digest from that manifest.
3. Get the referenced blob.

Write:

1. Check whether the empty config blob and cache-data blob already exist.
2. Start and complete uploads for missing blobs.
3. Publish an OCI image manifest with artifact type
   `application/vnd.ccache.entry`.

Delete:

1. Get the manifest headers.
2. Read `Docker-Content-Digest`.
3. Delete that manifest digest.

Only the manifest is deleted. The backend does not claim OCI blob garbage
collection; that remains a registry policy.

### GitHub Actions cache

For v2, read calls `GetCacheEntryDownloadURL` and downloads the returned signed
URL. Write calls `CreateCacheEntry`, uploads to the returned signed URL and
calls `FinalizeCacheEntryUpload`.

For v1, read uses the legacy artifact-cache lookup. Write reserves a cache,
patches the archive and finalizes it. The v1 and v2 request layouts are kept
separate in the backend.

After a write-side HTTP 403 or 429, ccache stores only an expiry time below
`$CCACHE_DIR/remote-storage/`. The filename is a hash of endpoint and prefix;
it contains neither a token nor the endpoint itself. `Retry-After` seconds are
used when supplied. Invalid or missing values use a 60-second fallback. A new
compiler invocation reads the same state and skips writes until expiry. Reads
continue.

## Multi-level behavior

The generic storage layer checks local ccache first unless `remote_only` is
set. It then visits `remote_storage` entries in their configured order.

`backfill=best-effort` is the default. A hit in a later backend is written to
earlier writable backends. `backfill=strict` turns a failed backfill into a
cache-operation failure. `backfill=disabled` performs no backfill.

`read-only` prevents writes to that backend but still permits reads. The Linux
OCI suite contains the intended Redis-to-OCI fallback and Redis backfill case;
it was not run on this Windows host.

## Security and diagnostics

Secrets must not be placed in shell history, `GITHUB_ENV`, `remote_storage`,
logs or this document. The manual GHA workflow keeps runtime values in the
`scripts/ci/ci.bats` process and its ccache child process; it does not export
them to `GITHUB_ENV`. Its `remote_debug` input selects `@debug=true` or
`@debug=false`. Debug output contains only the redacted URL, cache digest,
operation and HTTP status.

URLs are redacted for logging. Debug output reports operations and status but
not request or response bodies. The shared helpers centralize non-empty
environment lookup, boolean parsing, slash normalization, JSON string parsing,
HTTP URL primitives and diagnostic formatting. OCI and GHA protocol requests
remain separate.

| Code | Meaning |
| --- | --- |
| `CCACHE_NG-ERROR-GHA-0001` | No GHA endpoint before backend construction. |
| `CCACHE_NG-ERROR-GHA-0002` | No GHA runtime credential before backend construction. |
| `CCACHE_NG-ERROR-GHA-0021` | Direct GHA token configuration was rejected. |
| `CCACHE_NG-WARN-GHA-0006` | Write reserve received 403 or 429. |
| `CCACHE_NG-WARN-GHA-0019` | Write finalize received 403 or 429. |
| `CCACHE_NG-WARN-GHA-0020` | Cooldown state could not be persisted. |
| `CCACHE_NG-ERROR-OCI-0035` | Docker credential helper failed without exposing its output. |
| `CCACHE_NG-ERROR-OCI-0036` | More than one OCI credential source was configured. |
| `CCACHE_NG-ERROR-OCI-0037` | Direct OCI token configuration was rejected. |
| `CCACHE_NG-ERROR-OCI-0038` | systemd credential support is unavailable on this platform. |
| `CCACHE_NG-ERROR-OCI-0039` | systemd credential name is invalid. |
| `CCACHE_NG-ERROR-OCI-0040` | `CREDENTIALS_DIRECTORY` is not set for a systemd credential. |

## Local audit

Search scope: `src/ccache/storage/storage.cpp`, the built-in remote backends,
their tests, `scripts/ci/ci.sh`, `scripts/ci/ci.bats` and the GHA workflow. The
audit found identical GHA/OCI helpers for environment lookup, boolean parsing,
slash normalization and diagnostic formatting. They now live in
`remoteconfig.hpp` and
`remotediagnostics.hpp`. Shared HTTP URL and transport primitives live in
`httptransport.hpp`; JSON string parsing and Docker helper parsing live in
`credentials.cpp`. This is a statement about that inspected remote-storage
scope, not the whole repository.

Local Windows evidence on 2026-09-15:

```text
cmake -S . -B build-remote-storage-audit -G Ninja -DCMAKE_BUILD_TYPE=Release -DCCACHE_DEV_MODE=ON -DWARNINGS_AS_ERRORS=ON -DENABLE_IPO=ON -DOCI_STORAGE_BACKEND=ON -DGHA_STORAGE_BACKEND=ON
cmake --build build-remote-storage-audit --target unittest -j 8
ctest --test-dir build-remote-storage-audit --output-on-failure -R "^unittest$"
```

The build uses warnings-as-errors and IPO. The latest unit run completed 273
test cases successfully. `git diff --check` passed and the audited remote-
storage files had LF line endings. A targeted `dos2unix` conversion restored
LF line endings for the remaining tracked worktree files; `git ls-files --eol`
then found no CRLF or mixed line endings. The project format target could not
run on this host because its shell launcher requires a Linux Bash environment
that is not available here; it is not claimed as passed.

The focused Linux suites `test.remote_gha` and `test.remote_oci` are registered
only on non-Windows. They remain required before external acceptance.

## Apache acceptance commands

These are individual commands for a Linux acceptance host. They are a test
plan, not a recorded result. Run them only after approving the external GHA or
registry test. Run each action line separately; the following `printf` reports
that action's exit code, which must be 0. The output is limited to safe status
and statistics data. Start in the feature worktree.

```sh
export CCACHE_NG_SOURCE="$(pwd)"
printf 'set ccache source directory exit=%s\n' "$?"
test -f "$CCACHE_NG_SOURCE/CMakeLists.txt"
printf 'verify ccache source directory exit=%s\n' "$?"
mkdir -p /tmp/ccache-ng-apache-2.4.68
printf 'create test directory exit=%s\n' "$?"
cd /tmp/ccache-ng-apache-2.4.68
printf 'enter test directory exit=%s\n' "$?"
curl --fail --location --silent --show-error --output httpd-2.4.68.tar.bz2 https://downloads.apache.org/httpd/httpd-2.4.68.tar.bz2
printf 'download archive exit=%s\n' "$?"
curl --fail --location --silent --show-error --output httpd-2.4.68.tar.bz2.sha256 https://downloads.apache.org/httpd/httpd-2.4.68.tar.bz2.sha256
printf 'download checksum exit=%s\n' "$?"
sha256sum --check httpd-2.4.68.tar.bz2.sha256
printf 'verify checksum exit=%s\n' "$?"
tar --extract --bzip2 --file httpd-2.4.68.tar.bz2
printf 'extract archive exit=%s\n' "$?"
cd httpd-2.4.68
printf 'enter Apache source exit=%s\n' "$?"
./configure --enable-mods-shared=none
printf 'configure Apache exit=%s\n' "$?"
cmake -S "$CCACHE_NG_SOURCE" -B /tmp/ccache-ng-apache-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCCACHE_DEV_MODE=ON -DWARNINGS_AS_ERRORS=ON -DENABLE_IPO=ON -DOCI_STORAGE_BACKEND=ON -DGHA_STORAGE_BACKEND=ON
printf 'configure ccache exit=%s\n' "$?"
cmake --build /tmp/ccache-ng-apache-build --target ccache -j 8
printf 'build ccache exit=%s\n' "$?"
export CCACHE_DIR=/tmp/ccache-ng-apache-cache
printf 'set local cache directory exit=%s\n' "$?"
export CCACHE_REMOTE_STORAGE='gha://apache-acceptance @service-version=v2 @debug=false'
printf 'set remote storage exit=%s\n' "$?"
/tmp/ccache-ng-apache-build/ccache --clear
printf 'clear local cache exit=%s\n' "$?"
/tmp/ccache-ng-apache-build/ccache --zero-stats
printf 'zero statistics exit=%s\n' "$?"
make -j 8 CC=/tmp/ccache-ng-apache-build/ccache
printf 'cold build exit=%s\n' "$?"
/tmp/ccache-ng-apache-build/ccache --show-stats
printf 'cold statistics exit=%s\n' "$?"
/tmp/ccache-ng-apache-build/ccache --clear
printf 'clear local cache before warm build exit=%s\n' "$?"
make clean
printf 'clean Apache build exit=%s\n' "$?"
make -j 8 CC=/tmp/ccache-ng-apache-build/ccache
printf 'warm build exit=%s\n' "$?"
/tmp/ccache-ng-apache-build/ccache --show-stats
printf 'warm statistics exit=%s\n' "$?"
test "$(/tmp/ccache-ng-apache-build/ccache --show-stats | awk '$1 == "remote_storage_hit" { print $2 }')" -gt 0
printf 'verify warm remote hit exit=%s\n' "$?"
test -d /tmp/ccache-ng-apache-cache -a -d /tmp/ccache-ng-apache-build -a -d /tmp/ccache-ng-apache-2.4.68
printf 'verify named cleanup targets exit=%s\n' "$?"
rm -rf -- /tmp/ccache-ng-apache-cache /tmp/ccache-ng-apache-build /tmp/ccache-ng-apache-2.4.68
printf 'remove named test data exit=%s\n' "$?"
```

The download and checksum files come from the Apache HTTP Server distribution.
The test must record each command's exit code, cold `remote_storage_write`,
warm `remote_storage_hit`, the GitHub run ID and the cache UI/REST inspection
result. Do not replace these commands with a wrapper-script claim.

## External acceptance still required

1. Dispatch workflow phase `write` with a new non-secret `proof_id` and
   `remote_debug=false`; it stores `test/ccache_cache_test/proof.c` through
   GHA. Dispatch phase `read` with the same `proof_id`; it starts with an empty
   named local cache, proves a remote hit and rejects any remote write. Set
   `remote_debug=true` only when redacted backend diagnostics are needed.
2. Record both GitHub run IDs. Inspect the Actions cache UI and REST cache list.
3. Run the missing-GHA-token counter test: local compile exit 0,
   `CCACHE_NG-ERROR-GHA-0002` and exactly zero HTTP requests.
4. Run GHCR with a short-lived `GITHUB_TOKEN` and the Linux systemd credential
   store. Each path must read an entry made by the other.
5. Run Redis plus a local OCI registry: cold write, OCI remote hit, Redis
   outage, OCI fallback, Redis backfill and read-only behavior.
6. Remove only the named test caches, test packages and registry entries after
   checking that they exist.

## Limits

- GHA delete is unsupported by the cache protocol backend.
- HTTP-date forms of `Retry-After` are not parsed; they use the 60-second
  cooldown fallback.
- Registry-specific authentication exchanges and registry blob garbage
  collection are outside this backend.
- This document does not claim complete platform coverage, a bug-free result,
  successful GitHub execution or successful GHCR execution.
