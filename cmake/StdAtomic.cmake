# Check if std::atomic needs -latomic

set(LIBATOMIC_STATIC_PATH "" CACHE PATH "Directory containing static libatomic.a")

include(CheckCXXSourceCompiles)

# On macOS, std::atomic is built-in and doesn't require -latomic
if(APPLE)
  set(std_atomic_without_libatomic TRUE)
  message(STATUS "macOS detected: std::atomic is built-in")
else()
  set(
    check_std_atomic_source_code
    [=[
      #include <atomic>
      int main()
      {
        std::atomic<long long> x;
        ++x;
        (void)x.load();
        return 0;
      }
    ]=])

  check_cxx_source_compiles("${check_std_atomic_source_code}" std_atomic_without_libatomic)
endif()

if(NOT std_atomic_without_libatomic)
  set(CMAKE_REQUIRED_LIBRARIES atomic)
  check_cxx_source_compiles("${check_std_atomic_source_code}" std_atomic_with_libatomic)
  set(CMAKE_REQUIRED_LIBRARIES)
  if(NOT std_atomic_with_libatomic)
    message(FATAL_ERROR "Toolchain doesn't support std::atomic with nor without -latomic")
  else()
    if(STATIC_LINK)
      find_library(ATOMIC_STATIC NAMES libatomic.a PATHS /usr/lib /usr/local/lib ${LIBATOMIC_STATIC_PATH} NO_DEFAULT_PATH)
      if(ATOMIC_STATIC)
        message(STATUS "Linking static libatomic: ${ATOMIC_STATIC}")
        target_link_libraries(standard_settings INTERFACE ${ATOMIC_STATIC})
      else()
        message(WARNING "STATIC_LINK is set but static libatomic not found; falling back to -latomic")
        target_link_libraries(standard_settings INTERFACE atomic)
      endif()
    else()
      target_link_libraries(standard_settings INTERFACE atomic)
    endif()
  endif()
endif()
