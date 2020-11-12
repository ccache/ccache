# Check if std::atomic needs -latomic

include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_FLAGS ${CMAKE_CXX11_STANDARD_COMPILE_OPTION})
set(
  check_std_atomic_source_code
  [=[
    #include <atomic>
    int main()
    {
      std::atomic<long long> x;
      (void)x.load();
      return 0;
    }
  ]=])

check_cxx_source_compiles("${check_std_atomic_source_code}" std_atomic_without_libatomic)

if(NOT std_atomic_without_libatomic)
  set(CMAKE_REQUIRED_LIBRARIES atomic)
  check_cxx_source_compiles("${check_std_atomic_source_code}" std_atomic_with_libatomic)
  set(CMAKE_REQUIRED_LIBRARIES)
  if(NOT std_atomic_with_libatomic)
    message(FATAL_ERROR "Toolchain doesn't support std::atomic with nor without -latomic")
  else()
    target_link_libraries(standard_settings INTERFACE atomic)
  endif()
endif()

set(CMAKE_REQUIRED_FLAGS)
