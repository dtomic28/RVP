cmake_minimum_required(VERSION 3.10)

set(ASSIGNMENT_NAME "vaja02")

project(${ASSIGNMENT_NAME})

include_directories(${CMAKE_SOURCE_DIR}/${ASSIGNMENT_NAME}/include)

# Tell CMake where OpenCV's config file lives (inside your repo)
set(OpenCV_DIR "C:/Users/danijel/CLionProjects/RVP/vaja02/third_party/opencv/build")

# Find OpenCV from the vendored package
find_package(OpenCV REQUIRED)

set(SOURCES
    src/main.cpp
    include/bin_io.h
        # Add more source files here as needed
)

add_executable(${ASSIGNMENT_NAME} ${SOURCES})

target_link_libraries(${ASSIGNMENT_NAME} PRIVATE ${OpenCV_LIBS})

# Make sure the DLLs are next to your .exe after build
# (so it runs without editing PATH)
add_custom_command(TARGET ${ASSIGNMENT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${OpenCV_DIR}/x64/vc16/bin"
        "$<TARGET_FILE_DIR:${ASSIGNMENT_NAME}>"
)

# If you build both Debug/Release, keep multi-config behavior sane on MSVC:
if(MSVC)
    # Ensures /MD or /MDd matches OpenCV prebuilt (usually /MD)
    # Comment the next line if you explicitly want /MT static runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL$<$<CONFIG:Debug>:Debug>")
endif()
