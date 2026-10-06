cmake_minimum_required(VERSION 3.5)

project(PuhegWords-listener LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(listenwords main.cpp
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
    proof.cpp)

include(GNUInstallDirs)
install(TARGETS listenwords
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
