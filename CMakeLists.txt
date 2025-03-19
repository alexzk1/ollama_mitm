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

#Tests

#Add files to test here manually.
set(TO_TEST_FILES
    ${CMAKE_CURRENT_LIST_DIR}/socket.cpp
    ${CMAKE_CURRENT_LIST_DIR}/socket.hpp
    ${CMAKE_CURRENT_LIST_DIR}/runners.h
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
    source_group("tests / tests" FILES ${TESTS_LIST})
    source_group("tests / tested" FILES ${TO_TEST_FILES})
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
endif()
