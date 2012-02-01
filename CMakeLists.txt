cmake_minimum_required(VERSION 2.8)
project(ettercap C)

option(ENABLE_CURSES "Enable curses interface" ON)
option(ENABLE_GTK "Enable GTK interface" ON)
option(ENABLE_SSL "Enable SSL support" ON)
option(ENABLE_PLUGINS "Enable plugins support" ON)

set(INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX} CACHE PATH "Installation prefix")
set(INSTALL_SYSCONFDIR /etc CACHE PATH "System configuration directory")
set(INSTALL_LIBDIR ${INSTALL_PREFIX}/lib CACHE PATH "Library installation directory")
set(INSTALL_DATADIR ${INSTALL_PREFIX}/share CACHE PATH "Data installation directory")
set(INSTALL_EXECPREFIX ${INSTALL_PREFIX} CACHE PATH "")
set(INSTALL_BINDIR ${INSTALL_PREFIX}/bin CACHE PATH "Binary files installation directory")

# CFLAGS var is set here. -rdynamic flag is needed for plugins to work properly on some systems.
set(CMAKE_C_FLAGS_DEBUG "-O0 -ggdb3 -DDEBUG -Wall -pg" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS "-O2" CACHE STRING "" FORCE)

set(CMAKE_EXE_LINKER_FLAGS "-export-dynamic" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-export-dynamic -pg" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS "-export-dynamic" CACHE STRING "" FORCE)

include(CMakeFiles/CheckVariableExists.cmake)
include(CMakeFiles/variable_check.cmake)
include(CMakeFiles/os_test.cmake)
include(CMakeFiles/headers_check.cmake)
include(CMakeFiles/lib_check.cmake)

include_directories(include)
configure_file(include/config.h.cmake ${CMAKE_SOURCE_DIR}/include/config.h)

add_subdirectory(src)
if(HAVE_PLUGINS)
    if(OS_MINGW)
        message("Sorry, plugins support on Windows is currently unavailable")
    else(OS_MINGW)
        add_subdirectory(plug-ins)
    endif(OS_MINGW)
endif(HAVE_PLUGINS)
add_subdirectory(utils)
add_subdirectory(share)