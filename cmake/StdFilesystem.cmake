# Check if std::filesystem needs -lstdc++fs

include(CheckCXXSourceCompiles)

set(
  check_std_filesystem_source_code
  [=[
    #include <filesystem>
    int main(void)
    {
      return std::filesystem::is_regular_file(\"/\") ? 0 : 1;
    }
  ]=])

check_cxx_source_compiles("${check_std_filesystem_source_code}" std_filesystem_without_libfs)

if(NOT std_filesystem_without_libfs)
  set(CMAKE_REQUIRED_LIBRARIES stdc++fs)
  check_cxx_source_compiles("${check_std_filesystem_source_code}" std_filesystem_with_libfs)
  set(CMAKE_REQUIRED_LIBRARIES)
  if(NOT std_filesystem_with_libfs)
    message(FATAL_ERROR "Toolchain doesn't support std::filesystem with nor without -lstdc++fs")
  else()
    target_link_libraries(standard_settings INTERFACE stdc++fs)
  endif()
endif()
