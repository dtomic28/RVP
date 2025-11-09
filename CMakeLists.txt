cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 20)

set(GLOBAL_PROJECT_NAME "RacunalniskaVecpredstavnost")

project(${GLOBAL_PROJECT_NAME})

set(OpenCV_DIR "C:/Users/danijel/CLionProjects/RVP/vaja02/third_party/opencv/build")

add_subdirectory(vaja01)
add_subdirectory(vaja02)
