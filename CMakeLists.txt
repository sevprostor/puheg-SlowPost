cmake_minimum_required(VERSION 3.5)

project(Puheg-SlowPost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)

add_executable(slowpost main.cpp
    events.h
    events.cpp
    transport.h
    transport.cpp
    config.h
    config.cpp
    words.h
    words.cpp
    log.h
    file.h
    file.cpp
    ebparser.h
    ebparser.cpp
    process.h
    process.cpp
    proof.h
    proof.cpp
    compression.h
    compression.cpp
    routemanager.h
    routemanager.cpp
    address.h
    address.cpp
)

target_include_directories(slowpost PRIVATE ${ZSTD_INCLUDE_DIRS})
target_link_libraries(slowpost ${ZSTD_LIBRARIES})

include(GNUInstallDirs)
install(TARGETS slowpost
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
