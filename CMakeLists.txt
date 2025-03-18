cmake_minimum_required(VERSION 3.16)

project(ollama_mitm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ollama_mitm main.cpp)

include(GNUInstallDirs)
install(TARGETS ollama_mitm
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
