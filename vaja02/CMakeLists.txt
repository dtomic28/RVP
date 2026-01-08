cmake_minimum_required(VERSION 3.10)

set(ASSIGNMENT_NAME "vaja02")

project(${ASSIGNMENT_NAME})

include_directories(include)

set(SOURCES
        src/main.cpp
)

add_executable(${ASSIGNMENT_NAME} ${SOURCES})

target_include_directories(${ASSIGNMENT_NAME} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
)

find_package(OpenCV QUIET)

target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS})
target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})

message(STATUS "OpenCV version: ${OpenCV_VERSION}")