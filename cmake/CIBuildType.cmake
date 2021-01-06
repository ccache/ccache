# Add a build type called "CI" which is like RelWithDebInfo but with assertions
# enabled, i.e. without passing -DNDEBUG to the compiler.

set(CMAKE_CXX_FLAGS_CI ${CMAKE_CXX_FLAGS_RELWITHDEBINFO} CACHE STRING
 "Flags used by the C++ compiler during CI builds."
  FORCE)
set(CMAKE_C_FLAGS_CI ${CMAKE_C_FLAGS_RELWITHDEBINFO} CACHE STRING
  "Flags used by the C compiler during CI builds."
  FORCE)
set(CMAKE_EXE_LINKER_FLAGS_CI
  ${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} CACHE STRING
  "Flags used for linking binaries during CI builds."
  FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_CI
  ${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO} CACHE STRING
  "Flags used by the shared libraries linker during CI builds."
  FORCE)
mark_as_advanced(
  CMAKE_CXX_FLAGS_CI
  CMAKE_C_FLAGS_CI
  CMAKE_EXE_LINKER_FLAGS_CI
  CMAKE_SHARED_LINKER_FLAGS_CI)
# Update the documentation string of CMAKE_BUILD_TYPE for GUIs
set(CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING
  "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel CI."
  FORCE)

string(REGEX REPLACE "[/-]DNDEBUG" "" CMAKE_CXX_FLAGS_CI ${CMAKE_CXX_FLAGS_CI})
string(REGEX REPLACE "[/-]DNDEBUG" "" CMAKE_C_FLAGS_CI ${CMAKE_C_FLAGS_CI})
string(STRIP ${CMAKE_CXX_FLAGS_CI} CMAKE_CXX_FLAGS_CI)
string(STRIP ${CMAKE_C_FLAGS_CI} CMAKE_C_FLAGS_CI)
