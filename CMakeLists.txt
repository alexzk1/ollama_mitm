cmake_minimum_required(VERSION 4.00)
include(FetchContent)
include(CMakePrintHelpers)

project(ollama_mitm LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Helper function to create include symlinks for a target
# Usage: create_include_symlinks(<target> <source_dir> <virtual_namespace>)
function(create_include_symlinks target source_dir virtual_namespace)
  file(GLOB HEADER_FILES
    RELATIVE ${source_dir}
    ${source_dir}/*.hpp
    ${source_dir}/*.h
  )

  set(fake_include_dir ${CMAKE_BINARY_DIR}/fake_includes/${virtual_namespace})

  file(MAKE_DIRECTORY ${fake_include_dir})

  foreach(header ${HEADER_FILES})
    set(src "${source_dir}/${header}")
    set(dst "${fake_include_dir}/${header}")

    if(NOT EXISTS ${dst})
      execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${src} ${dst}
      )
    endif()
  endforeach()

  target_include_directories(${target} INTERFACE ${CMAKE_BINARY_DIR}/fake_includes)
endfunction()


# Fetch and build the ollama-hpp library from GitHub
FetchContent_Declare(
  ollama-hpp
  GIT_REPOSITORY    https://github.com/jmont-dev/ollama-hpp.git
  GIT_TAG           v0.9.5
)

#Library does not provide cmake files.
FetchContent_MakeAvailable(ollama-hpp)
message(STATUS "ollama-hpp_SOURCE_DIR: ${ollama-hpp_SOURCE_DIR}")
if(NOT EXISTS "${ollama-hpp_SOURCE_DIR}/include/ollama.hpp")
  message(FATAL_ERROR "ollama-hpp include directory not found at ${ollama-hpp_SOURCE_DIR}")
endif()
#So we put it into own library...
add_library(ollama-hpp INTERFACE)
#...and setup include alias <ollama/...h>
create_include_symlinks(
  ollama-hpp
  ${ollama-hpp_SOURCE_DIR}/include
  ollama
)


#Our souce code
file(GLOB BASE_FOLDER
        *.h
        *.hpp
        *.cpp
    )

file(GLOB COMMON_FOLDER
        common/*.h
        common/*.hpp
        common/*.cpp
    )

file(GLOB NETWORK_FOLDER
        network/*.h
        network/*.hpp
        network/*.cpp
    )
add_executable(ollama_mitm  ${BASE_FOLDER} ${COMMON_FOLDER} ${NETWORK_FOLDER})
target_include_directories(ollama_mitm PUBLIC
                    ${CMAKE_CURRENT_LIST_DIR}
                )
target_link_libraries(ollama_mitm PRIVATE ollama-hpp)

source_group("source / main" FILES ${BASE_FOLDER})
source_group("source / common" FILES ${COMMON_FOLDER})
source_group("source / network" FILES ${NETWORK_FOLDER})

#Tests

#Add files to test here manually.
set(TO_TEST_FILES
    ${COMMON_FOLDER}
    ${NETWORK_FOLDER}
)

#Automatically list test files.
file(GLOB_RECURSE TESTS_LIST
     ${CMAKE_CURRENT_LIST_DIR}/tests/*.h
     ${CMAKE_CURRENT_LIST_DIR}/tests/*.hpp
     ${CMAKE_CURRENT_LIST_DIR}/tests/*.cpp
    )
list(LENGTH TESTS_LIST TESTS_LIST_FILES_COUNT)
if (TESTS_LIST_FILES_COUNT GREATER 0)
    find_package(GTest REQUIRED)
    add_executable(ollama_mitm_tests
                   ${TESTS_LIST} ${TO_TEST_FILES}
    )
    target_link_libraries(ollama_mitm_tests PRIVATE
                        gtest
                        gmock
    )
    target_include_directories(ollama_mitm_tests PUBLIC
                        ${CMAKE_CURRENT_LIST_DIR}/tests/
                        ${CMAKE_CURRENT_LIST_DIR}
                    )
    add_test(NAME ollama_mitm_tests COMMAND ollama_mitm_tests)
    source_group("tests" FILES ${TESTS_LIST})
endif()
