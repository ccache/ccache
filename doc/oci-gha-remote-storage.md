# OCI and GitHub Actions remote storage

This document is the single source of truth for the OCI/GHCR and GitHub
Actions cache remote-storage module. It distinguishes implemented behavior,
automated coverage, integration evidence and work that remains unproven.

The normative terms MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as
defined by RFC 2119.

## Scope and requirements

The module provides these remote-storage schemes:

- `oci://REGISTRY/REPOSITORY[/ccache/PREFIX]` for OCI-compatible registries.
- `gha://[PREFIX]` for the GitHub Actions cache data service.

GHCR MUST be treated as a regular OCI registry. Registry-specific code MUST
NOT be added unless required by the OCI Distribution API. `remote_storage` and
`CCACHE_REMOTE_STORAGE` remain the ordering source of truth: entries MUST be
listed from the fastest backend to the slowest backend.

The local ccache is consulted before remote storage unless ccache is configured
as remote-only. A slower remote hit SHOULD backfill an earlier, writable remote
backend. A remote cache failure MUST NOT make compilation fail when local cache
or a later remote backend can serve the operation, except when an operator
selects strict backfill.

## Implemented behavior

### Cache keys and privacy

The default key is the complete ccache digest in lowercase base-16. It MUST NOT
be truncated and MUST NOT automatically add repository, branch, runner, user,
hostname or address information. An operator MAY add a deliberate prefix using
`@prefix=VALUE`.

Tokens MUST be supplied through environment variables, repository variables or
secrets. Documentation, tests, errors and debug logs MUST NOT expose tokens,
credentials, response bodies, private addresses or private paths. `@token=...`
is available for isolated tests only and MUST NOT be used in shared
configuration.

### OCI protocol

For an OCI entry, the backend retrieves an OCI manifest and then the manifest's
cache-data blob. To store an entry it uploads the zero-byte config and cache
blob, then publishes an OCI manifest with artifact type
`application/vnd.ccache.entry`. Removal resolves the manifest digest and sends
an OCI manifest deletion request.

OCI connections MUST use HTTPS unless `@insecure=true` is deliberately selected
for an isolated local registry test. Supported OCI attributes are `@token`,
`@token-env`, `@prefix`, `@insecure`, `@debug`, `@connect-timeout` and
`@operation-timeout`.

### GitHub Actions cache protocol

The `gha://` adapter obtains its endpoint from `ACTIONS_RESULTS_URL`, falling
back to `ACTIONS_CACHE_URL`, and obtains its bearer credential from
`ACTIONS_RUNTIME_TOKEN`, falling back to `ACTIONS_ID_TOKEN_REQUEST_TOKEN`.
`@url`, `@url-env`, `@token` and `@token-env` explicitly override those values.
The endpoint and token are required; missing values produce
`CCACHE_NG-ERROR-GHA-0001` or `CCACHE_NG-ERROR-GHA-0002`.

When v2 is selected by `ACTIONS_CACHE_SERVICE_V2` or `@service-version=v2`, the
adapter performs this sequence:

1. `GetCacheEntryDownloadURL` with the complete cache key and cache version.
2. On a hit, downloads the returned signed URL.
3. On a store, calls `CreateCacheEntry` with the key and version.
4. Uploads the cache bytes to the returned signed URL.
5. Calls `FinalizeCacheEntryUpload` with key, byte count and version.

The legacy v1 path remains selectable with `@service-version=v1`. The supported
GHA attributes are `@url`, `@url-env`, `@token`, `@token-env`, `@prefix`,
`@service-version`, `@debug`, `@connect-timeout` and `@operation-timeout`.

`gha://` is designed for a GitHub Actions job where the runner provides its
runtime endpoint and short-lived runtime token. A normal personal access token,
GitHub App token or `gh` login MUST NOT be documented as a substitute: GitHub's
documented REST cache API is a management API, not the cache data upload and
download protocol. Therefore a durable direct local-machine connection to the
hosted GitHub Actions cache is NOT YET PROVEN and is not claimed by this module.
For a cross-machine durable cache, OCI/GHCR is the implemented storage option.

### Rate limits and failure behavior

Every module diagnostic MUST use the format
`CCACHE_NG-<SEVERITY>-<SERVICE>-<NUMBER>`. The code identifies the relevant
failure path; the same code MAY appear again when an operation is attempted
again. This is intentional and MUST NOT be replaced with blanket log-once
suppression.

GHA debug output is enabled by `@debug=true` or `ACTIONS_STEP_DEBUG`; an
explicit `@debug=false` overrides the environment. Debug messages report the
operation, status and redacted URL, but MUST NOT report a token or response
body.

After a `403` or `429` from GHA cache-entry creation or finalization, the
backend emits `CCACHE_NG-WARN-GHA-0006` or `CCACHE_NG-WARN-GHA-0019`, disables
further GHA writes for that backend instance and returns without aborting the
compile. Reads and later configured backends MAY continue. This prevents a
rate-limited backend from receiving repeated write attempts.

## Multi-level policy

`backfill=best-effort`, `backfill=strict` and `backfill=disabled` control
write-back from a later hit to preceding remote backends. The default MUST be
`best-effort`:

- `best-effort` records the failure and continues.
- `strict` fails the cache operation when the backfill fails.
- `disabled` does not write back.

`read-only` backends MAY be read but MUST NOT be written. A backend marked as
failed is skipped for the remaining ccache process; this avoids repeated
transport calls after a confirmed backend failure without suppressing unrelated
operation diagnostics.

## Configuration examples

The examples use placeholders only. They MUST NOT be copied with real secrets
into version-controlled files.

```ini
# Fast Redis, durable OCI/GHCR, then the Actions job cache.
remote_storage = redis://cache.example.invalid:6379 oci://ghcr.io/OWNER/REPOSITORY/ccache/team @token-env=CCACHE_OCI_TOKEN gha://team @service-version=v2 @debug=false
```

```sh
# The Actions runner normally provides both values. This is an isolated test
# configuration, not a substitute for GitHub-issued runtime credentials.
export CCACHE_REMOTE_STORAGE='gha://example @url-env=TEST_GHA_URL @token-env=TEST_GHA_TOKEN @service-version=v2'
```

## Evidence and traceability

| Claim | Status | Authoritative evidence |
| --- | --- | --- |
| `gha://` parses endpoint, token, prefix, version, debug and timeout attributes. | Automated | `src/ccache/storage/remote/ghastorage.cpp`; `test/suites/remote_gha.bash` |
| GHA v2 performs lookup, signed download, create, signed upload and finalize. | Automated | `src/ccache/storage/remote/ghastorage.cpp`; local v2 mock in `test/gha-cache-server` |
| GHA `403` and `429` stop further writes for the backend instance. | Automated | `test/suites/remote_gha.bash`; mock reserve-request counter |
| GHA diagnostics use stable, redacted identifiers. | Automated | `src/ccache/storage/remote/ghastorage.cpp`; unit tests for redaction and configuration |
| OCI and Redis fallback/backfill operate against an isolated local registry. | Automated integration | `test/suites/remote_oci.bash` |
| Apache source compiles through `gha://`, writes cold entries, clears local ccache and receives warm remote hits. | Integration | `test/run-gha-apache-smoke`, `test/run-gha-apache-container`, recorded result below |
| Hosted GitHub Actions runtime accepts this adapter in a workflow. | Not yet validated | Requires a real workflow run with its ephemeral runtime credential. |
| A local Linux machine can use hosted GHA cache with a durable non-runtime credential. | Not supported claim | GitHub's documented REST cache endpoints manage caches but do not expose cache-data upload/download. |
| GHCR, Docker Hub or GitLab Registry have been validated against external services. | Not yet validated | No external registry credentials or services were used for this evidence set. |

## Apache GHA integration record

The integration runner fetches Apache httpd `2.4.68` from the official Apache
download location and verifies this SHA-256 before extraction:

```text
ed9a9d4500fb48bb28eaffb3ba71d06ccf86d498fa13ab9f781da010cc488498
```

It configures Apache with `--enable-mods-shared=none`, so this is a real Apache
source build but not a claim that every optional Apache module was built. The
runner builds ccache in Release mode with explicit `-O3 -DNDEBUG`, development
warnings as errors and IPO enabled. It builds using `-j12`.

The runner then:

1. Starts an isolated local GHA v2 mock.
2. Compiles the configured Apache source using ccache and records cold remote
   writes.
3. Clears only that test's `CCACHE_DIR`, zeros statistics and cleans Apache
   objects.
4. Compiles Apache again and requires a positive `remote_storage_hit` count.
5. Removes the temporary test directory and mock process through an EXIT trap.

The recorded successful result was:

```text
CCACHE_DEV_MODE:UNINITIALIZED=ON
CMAKE_CXX_FLAGS_RELEASE:STRING=-O3 -DNDEBUG
CMAKE_C_FLAGS_RELEASE:STRING=-O3 -DNDEBUG
WARNINGS_AS_ERRORS:BOOL=ON
-Werror
-flto
APACHE_SOURCE_SHA256=ed9a9d4500fb48bb28eaffb3ba71d06ccf86d498fa13ab9f781da010cc488498
Statistics zeroed
Statistics zeroed
APACHE_GHA_PROOF apache=2.4.68 cold_remote_writes=419 cold_remote_hits=0 warm_remote_writes=1 warm_remote_hits=209 local_cache_cleared=yes

--- Apache GHA proof status: 0 ---
```

This proves the test's GHA v2 data-path sequence and local-cache clearing
behavior. It MUST NOT be read as evidence of a hosted GitHub cache entry,
GitHub cache retention, GitHub cache scope, or production authentication.

## Reproducible validation

On a Linux Docker host, create a byte-preserving source archive of this
worktree, transfer it without text conversion, and invoke:

```sh
bash test/run-gha-apache-remote-proof SOURCE_ARCHIVE PROOF_LOG STATUS_FILE
```

The runner uses one explicitly named, `--rm` container and fails when download,
checksum, configuration, build, cold-write assertion, warm-hit assertion or
container execution fails. Its exit status is written to `STATUS_FILE`; command
output remains in `PROOF_LOG` for inspection. It MUST NOT be used to inspect,
stop or remove unrelated containers.

Focused automated verification uses:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCCACHE_DEV_MODE=ON \
  -DWARNINGS_AS_ERRORS=ON -DENABLE_IPO=ON \
  -DOCI_STORAGE_BACKEND=ON -DGHA_STORAGE_BACKEND=ON
cmake --build build --target ccache unittest -j12
ctest --test-dir build --output-on-failure -R 'unittest|test.remote_(oci|gha)'
```

The project also follows the upstream ccache remote-storage grammar and
ordering rules described in the [ccache manual](https://ccache.dev/manual/latest.html).
GitHub's cache access and management boundaries are described in the
[GitHub Actions cache documentation](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching)
and the [REST cache API documentation](https://docs.github.com/en/rest/actions/cache).
