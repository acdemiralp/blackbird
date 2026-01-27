cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project               (blackbird VERSION 1.0 LANGUAGES CXX)
set                   (CMAKE_CXX_STANDARD 20)

if   (MSVC)
  add_compile_options (/W4)
endif()

# User-space version
add_executable        (${PROJECT_NAME} WIN32 main.cpp)

install               (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)

# Kernel-space driver (informational only, requires WDK to build)
option(BUILD_DRIVER_INFO "Include driver information in build" ON)
if(BUILD_DRIVER_INFO)
  add_subdirectory(driver)
endif()