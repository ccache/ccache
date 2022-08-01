if(WIN32)
  if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(program_files "$ENV{ProgramFiles}")

    # For 32 bit builds.
    if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND ENV{ProgramFiles\(x86\)})
      set(program_files "$ENV{ProgramFiles\(x86\)}")
    endif()

    if(NOT program_files)
      if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(program_files "/Program Files")
      else()
        set(program_files "/Program Files (x86)")
      endif()
    endif()

    file(TO_CMAKE_PATH "${program_files}/ccache" CMAKE_INSTALL_PREFIX)

    set(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE STRING "System-wide installation prefix" FORCE)
  endif()

  if(NOT CMAKE_INSTALL_SYSCONFDIR)
    set(program_data "$ENV{ALLUSERSPROFILE}")

    if(NOT program_data)
      set(program_data "/ProgramData")
    endif()

    file(TO_CMAKE_PATH "${program_data}/ccache" CMAKE_INSTALL_SYSCONFDIR)

    set(CMAKE_INSTALL_SYSCONFDIR "${CMAKE_INSTALL_SYSCONFDIR}" CACHE PATH "System-wide config file location" FORCE)
  endif()

  set(CMAKE_INSTALL_BINDIR     "" CACHE PATH "executables subdirectory" FORCE)
  set(CMAKE_INSTALL_SBINDIR    "" CACHE PATH "system administration executables subdirectory" FORCE)
  set(CMAKE_INSTALL_LIBEXECDIR "" CACHE PATH "dependent executables subdirectory" FORCE)
  set(CMAKE_INSTALL_LIBDIR     "" CACHE PATH "object libraries subdirectory" FORCE)
endif()

include(GNUInstallDirs)
