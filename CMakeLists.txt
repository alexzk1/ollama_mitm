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
