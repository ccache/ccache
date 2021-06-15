# Ccache architecture

## Code structure

The ccache code base has grown organically through the years. This section
describes the directory structure that the project aims to transform the source
code into in the long run to make it easier to understand and work with.

* `ci`: Utility scripts used in CI.
* `cmake`: CMake scripts.
* `doc`: Documentation.
* `dockerfiles`: Dockerfiles that specify different environments of interest for
  ccache.
* `misc`: Miscellaneous utility scripts, example files, etc.
* `src/compiler`: Knowledge about things like compiler options, compiler
  behavior, preprocessor output format, etc. Ideally this code should in the
  future be refactored into compiler-specific frontends, such as GCC, Clang,
  MSVC, etc.
* `src/compression`: Compression formats.
* `src/framework`: Everything not part of other directories.
* `src/storage`: Storage backends.
* `src/storage/primary`: Code for the primary storage backend.
* `src/storage/secondary`: Code for secondary storage backends.
* `src/third_party`: Bundled third party code.
* `src/util`: Generic utility functionality that does not depend on
  ccache-specific things.
* `test`: Integration test suite which tests the ccache binary in different
  scenarios.
* `unittest`: Unit test suite which typically tests individual functions.
