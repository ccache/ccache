option(ENABLE_CPPCHECK "Enable static analysis with Cppcheck" OFF)
if(ENABLE_CPPCHECK)
  find_program(CPPCHECK_EXE cppcheck)
  mark_as_advanced(CPPCHECK_EXE) # Don't show in CMake UIs
  if(CPPCHECK_EXE)
    set(CMAKE_CXX_CPPCHECK
        ${CPPCHECK_EXE}
        --suppressions-list=${CMAKE_SOURCE_DIR}/misc/cppcheck-suppressions.txt
        --inline-suppr
        -q
        --enable=all
        --force
        --std=c++17
        -I ${CMAKE_SOURCE_DIR}
        --template="cppcheck: warning: {id}:{file}:{line}: {message}"
        -i src/third_party)
  else()
    message(WARNING "Cppcheck requested but executable not found")
  endif()
endif()

option(ENABLE_CLANG_TIDY "Enable static analysis with Clang-Tidy" OFF)
if(ENABLE_CLANG_TIDY)
  find_program(CLANGTIDY clang-tidy)
  if(NOT CLANGTIDY)
    message(SEND_ERROR "Clang-Tidy requested but executable not found")
  endif()
endif()
