# OCI and GitHub Actions remote storage

This document is the source of truth for the OCI/GHCR and GitHub Actions cache
remote storage module.

## Scope

The module provides two remote storage schemes:

- `oci://REGISTRY/OWNER/NAME[/ccache/PREFIX]` for OCI-compatible registries.
- `gha://[PREFIX]` for the GitHub Actions cache service.

GHCR MUST be treated as a regular OCI registry. Registry-specific behavior MUST
NOT leak into the generic OCI backend unless the OCI Distribution API requires
it.

## Requirements

- Redis MUST remain the fast cache level when it is configured before OCI or
  GHA storage.
- OCI storage MUST support a GHCR repository without GHCR-specific code paths.
- GHA storage MUST use GitHub Actions runtime configuration and MUST NOT make a
  compilation fail because cache upload is unavailable.
- A hit in a later writable level SHOULD backfill earlier writable levels.
- Failures MUST be counted, MUST have stable unique codes and MUST NOT repeat
  within a backend instance.

## Source Of Truth

| Concern | Source of truth |
| --- | --- |
| Backend order | `remote_storage` or `CCACHE_REMOTE_STORAGE` order |
| Default cache key | Full ccache digest in lowercase hexadecimal |
| OCI registry protocol | OCI Distribution API manifest and blob endpoints |
| GHA runtime endpoint and token | GitHub Actions runtime environment |
| Write behavior | `read-only` and `backfill` configuration properties |
| Debug behavior | `@debug` and `ACTIONS_STEP_DEBUG` |
| Error lookup | Stable `CCACHE-OCI`, `CCACHE-GHA` and `CCACHE-REMOTE` codes |

## Configuration

The `remote_storage` setting and `CCACHE_REMOTE_STORAGE` remain the ordering
source of truth. Backends MUST be listed from fastest to slowest.

Example:

```ini
remote_storage = redis://cache.example.invalid:6379 oci://ghcr.io/OWNER/NAME/ccache/prod @token-env=CCACHE_OCI_TOKEN gha://prod @debug=false
```

The default cache key MUST be the full ccache digest formatted as lowercase hex.
The default key MUST NOT include repository, branch, runner, user, hostname or IP
address information. Operators MAY add an explicit `@prefix=VALUE`.

## Secrets

Tokens MUST come from environment variables, repository variables or secrets.
Documentation, tests, logs and errors MUST NOT include real tokens, private IP
addresses, usernames, repository-private paths or credentials.

Supported secret attributes:

- `@token-env=NAME` reads a token from an environment variable.
- `@token=VALUE` is supported for controlled tests only and MUST NOT be used in
  shared configuration.
- `@url-env=NAME` is supported by `gha://` for tests and custom runners.
- `@service-version=v1` or `@service-version=v2` selects a cache service
  version. The default MUST select v2 when `ACTIONS_CACHE_SERVICE_V2` is set.
- `@insecure=true` MAY be used only for an isolated local OCI registry test.
  OCI registry connections MUST use HTTPS by default.

## Error reporting

Operational failures MUST be logged at most once per backend instance and run.
Each failure message MUST include a stable error code so that the code path can
be found from logs. URLs in errors and debug output MUST redact credentials and
numeric hosts. Current prefixes are:

- `CCACHE-OCI-0001` through `CCACHE-OCI-0031`
- `CCACHE-GHA-0001` through `CCACHE-GHA-0019`
- `CCACHE-REMOTE-0001` through `CCACHE-REMOTE-0003`

Debug logging MAY be enabled with `@debug=true` or by enabling GitHub Actions
step debugging, which sets `ACTIONS_STEP_DEBUG`. `@debug=false` MUST override
the runtime default. Debug logs MUST still redact tokens, credentials and
numeric hosts.

## Backfill behavior

Remote storage order is authoritative. A hit in a slower backend SHOULD backfill
earlier faster levels when those levels are writable. Backfill failures SHOULD be
best effort by default and MUST NOT fail a compile while another cache level can
serve or store the entry.

Policy values are configured with `backfill=VALUE`:

- `best-effort`: continue on backfill errors and count the failure.
- `strict`: fail when backfill fails.
- `disabled`: do not backfill faster remote levels.

The default policy MUST be `best-effort`.

## Tests

Local tests MUST cover:

- `oci://` and `gha://` configuration parsing.
- Token lookup through environment variables without token output.
- URL, attribute and error redaction.
- Full digest key generation without truncation.
- Read-only behavior and missing-write-permission handling.
- Non-repeated error logging and stable error codes.
- Redis failure fallback and OCI-to-Redis backfill.

Local OCI registry integration tests MUST be preferred for required registry
coverage. Docker Hub and GitLab registry tests MAY run only when safe secrets are
present.

Set `OCI_TEST_REGISTRY` to the host and port of an isolated local registry to
run `test.remote_oci`. The suite requires `redis-server` and `redis-cli` (or
their Valkey equivalents) for fallback and backfill coverage. It MUST use
`@insecure=true` only for that local test registry.

`test.remote_gha` MUST use its local GHA v2 mock server. It covers cache entry
creation, signed upload and download URLs, finalization and a rate-limited
write without requiring an Actions token or external service.

## Build

Release verification SHOULD use an optimized build with developer warnings as
errors. If supported by the toolchain, `ENABLE_IPO=ON` SHOULD be used.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCCACHE_DEV_MODE=ON \
  -DWARNINGS_AS_ERRORS=ON -DENABLE_IPO=ON \
  -DOCI_STORAGE_BACKEND=ON -DGHA_STORAGE_BACKEND=ON
cmake --build build --target ccache unittest -j12
ctest --test-dir build --output-on-failure -R 'unittest|test.remote_(oci|gha)'
```
