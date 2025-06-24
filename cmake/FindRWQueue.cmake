mark_as_advanced(RWQUEUE_INCLUDE_DIR)
mark_as_advanced(RWQUEUE_LIBRARY)

if(DEP_RWQUEUE STREQUAL "BUNDLED")
  message(STATUS "Using bundled ReaderWriterQueue as requested")
else()
  find_path(RWQUEUE_INCLUDE_DIR readerwriterqueue.h)
  find_library(RWQUEUE_LIBRARY readerwriterqueue)
  if(RWQUEUE_INCLUDE_DIR)
    file(READ "${RWQUEUE_INCLUDE_DIR}/readerwriterqueue.hpp" _readerwriterqueue_h)
    string(REGEX MATCH "#define RWQUEUE_VERSION_STRING \"([0-9]+).([0-9]+).*([0-9]+)\"" _ "${_readerwriterqueue_h}")
    set(_readerwriterqueue_version_string "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    if(NOT "${CMAKE_MATCH_0}" STREQUAL "" AND "${_readerwriterqueue_version_string}" VERSION_GREATER_EQUAL "${ReaderWriterQueue_FIND_VERSION}")
      if(RWQUEUE_LIBRARY)
        message(STATUS "Using system ReaderWriterQueue (${RWQUEUE_LIBRARY})")
        add_library(dep_readerwriterqueue UNKNOWN IMPORTED)
        set_target_properties(
          dep_rwqueue
          PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${RWQUEUE_INCLUDE_DIR}"
          IMPORTED_LOCATION "${RWQUEUE_LIBRARY}"
        )
      endif()
      register_dependency(ReaderWriterQueue "SYSTEM (${RWQUEUE_LIBRARY})" "${_readerwriterqueue_version_string}")
    endif()
  endif()
  if(NOT TARGET dep_readerwriterqueue)
    message(STATUS "Using bundled ReaderWriterQueue since ReaderWriterQueue>=${ReaderWriterQueue_FIND_VERSION} was not found locally")
  endif()
endif()
