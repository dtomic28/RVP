cmake_minimum_required(VERSION 3.10)

set(ASSIGNMENT_NAME "vaja01")

project(${ASSIGNMENT_NAME})

include_directories(${CMAKE_SOURCE_DIR}/${ASSIGNMENT_NAME}/include)

set(SOURCES
    src/main.cpp
    # Add more source files here as needed
)

add_executable(${ASSIGNMENT_NAME} ${SOURCES})

