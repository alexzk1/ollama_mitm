cmake_minimum_required(VERSION 3.16)

project(ollama_mitm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#Our souce code
file(GLOB BASE_FOLDER
        *.h
        *.hpp
        *.cpp
    )

add_executable(ollama_mitm  ${BASE_FOLDER})

