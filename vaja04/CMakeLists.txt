cmake_minimum_required(VERSION 3.10)

set(ASSIGNMENT_NAME "vaja04")

project(${ASSIGNMENT_NAME})

include_directories(include)

set(SOURCES
        src/main.cpp
        include/bin_io.h
)

add_executable(${ASSIGNMENT_NAME} ${SOURCES})

