
# College Assignments Template

This repository serves as a template for organizing and building C++ college assignments. It is structured to support multiple assignments, each with its own source code, headers, and build configuration using CMake.

## Structure

Each assignment (e.g., `vaja01`, `vaja02`, etc.) will have its own folder with the following structure:

```
/CollegeAssignments
    /vaja01
        /src
            main.cpp           # Source files for the assignment
        /include               # Header files for the assignment
        CMakeLists.txt         # CMake configuration for the assignment
    /vaja02
        /src
            main.cpp           # Source files for the assignment
        /include               # Header files for the assignment
        CMakeLists.txt         # CMake configuration for the assignment
    CMakeLists.txt (global)     # Global CMakeLists.txt for managing all assignments
```

### Global `CMakeLists.txt`

The global `CMakeLists.txt` file tracks all assignments and builds them:

```cmake
cmake_minimum_required(VERSION 3.10)
set(GLOBAL_PROJECT_NAME "CollegeAssignments")
project(${GLOBAL_PROJECT_NAME})

# Add subdirectories for each assignment
add_subdirectory(vaja01)
add_subdirectory(vaja02)
```

### Individual Assignment `CMakeLists.txt`

Each assignment has its own `CMakeLists.txt` file. Here's a template:

```cmake
cmake_minimum_required(VERSION 3.10)
set(ASSIGNMENT_NAME "vaja01")
project(${ASSIGNMENT_NAME})

include_directories(${CMAKE_SOURCE_DIR}/${ASSIGNMENT_NAME}/include)

set(SOURCES
    src/main.cpp
    # Add more source files here
)

add_executable(${ASSIGNMENT_NAME} ${SOURCES})
```
