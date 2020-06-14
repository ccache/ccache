option(ENABLE_CPPCHECK "Enable static analysis with cppcheck" OFF)
if(ENABLE_CPPCHECK)
  if(${CMAKE_VERSION} VERSION_LESS "3.10")
    message(WARNING "CppCheck requires cmake 3.10")
  else()
    find_program(CPPCHECK_EXE cppcheck)
    mark_as_advanced(CPPCHECK_EXE) # don't show in ccmake
    if(CPPCHECK_EXE)
      set(CMAKE_CXX_CPPCHECK
          ${CPPCHECK_EXE}
          --suppressions-list=${CMAKE_SOURCE_DIR}/misc/cppcheck-suppressions.txt
          --inline-suppr
          -q
          --enable=all
          --force
          --std=c++11
          -I ${CMAKE_SOURCE_DIR}
          --template="cppcheck: warning: {id}:{file}:{line}: {message}"
          -i src/third_party)
    else()
      message(WARNING "cppcheck requested but executable not found")
    endif()
  endif()
endif()

option(ENABLE_CLANG_TIDY "Enable static analysis with clang-tidy" OFF)
if(ENABLE_CLANG_TIDY)
  if(${CMAKE_VERSION} VERSION_LESS "3.6")
    message(WARNING "clang-tidy requires cmake 3.6")
  else()
    find_program(CLANGTIDY clang-tidy)
    if(CLANGTIDY)
      set(CMAKE_CXX_CLANG_TIDY ${CLANGTIDY})
    else()
      message(SEND_ERROR "clang-tidy requested but executable not found")
    endif()
  endif()
endif()
