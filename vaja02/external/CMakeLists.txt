# This file is only entered if OpenCV wasn't found already.

# If you need extra modules:
# FetchContent_Declare(
#   opencv_contrib
#   GIT_REPOSITORY https://github.com/opencv/opencv_contrib.git
#   GIT_TAG        4.9.0
# )
# set(OPENCV_EXTRA_MODULES_PATH "${opencv_contrib_SOURCE_DIR}/modules" CACHE PATH "" FORCE)

FetchContent_MakeAvailable(opencv)

# After this, OpenCV defines its usual variables (OpenCV_FOUND, OpenCV_INCLUDE_DIRS, OpenCV_LIBS).
# We make sure parent scope can see them.
set(OpenCV_FOUND ${OpenCV_FOUND} PARENT_SCOPE)
set(OpenCV_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS} PARENT_SCOPE)
set(OpenCV_LIBS ${OpenCV_LIBS} PARENT_SCOPE)



