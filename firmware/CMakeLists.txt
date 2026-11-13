# Host build: unit tests and the WAV renderer. The Teensy firmware is built
# with PlatformIO (platformio.ini), not with this file.
cmake_minimum_required(VERSION 3.16)
project(lwave_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

file(GLOB_RECURSE LWAVE_SOURCES CONFIGURE_DEPENDS lib/lwave/src/*.cpp)
add_library(lwave STATIC ${LWAVE_SOURCES})
target_include_directories(lwave PUBLIC lib/lwave/src)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS test/host/*.cpp)
add_executable(lwave_tests ${TEST_SOURCES})
target_link_libraries(lwave_tests lwave)

add_executable(lwave_render tools/render/render.cpp tools/render/wav.cpp)
target_link_libraries(lwave_render lwave)
