# Ccache architecture

## Code structure

### Top-level directories

* `ci`: Utility scripts used in CI.
* `cmake`: CMake scripts.
* `doc`: Documentation.
* `dockerfiles`: Dockerfiles that specify different environments of interest for
  ccache.
* `misc`: Miscellaneous utility scripts, example files, etc.
* `src`: Source code. See below.
* `test`: Integration test suite which tests the ccache binary in different
  scenarios.
* `unittest`: Unit test suite which typically tests individual functions.

### Subdirectories of `src`

This section describes the directory structure that the project aims to
transform the `src` directory into in the long run to make the code base easier
to understand and work with. In other words, this is work in progress.

* `compiler`: Knowledge about things like compiler options, compiler behavior,
  preprocessor output format, etc. Ideally this code should in the future be
  refactored into compiler-specific frontends, such as GCC, Clang, NVCC, MSVC,
  etc.
* `core`: Everything not part of other directories.
* `storage`: Storage backends.
* `storage/primary`: Code for the primary storage backend.
* `storage/secondary`: Code for secondary storage backends.
* `third_party`: Bundled third party code.
* `util`: Generic utility functionality that does not depend on ccache-specific
  things.
