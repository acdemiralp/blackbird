# Blackbird Kernel Driver CMake Configuration
# Note: Building Windows kernel drivers with CMake requires the Windows Driver Kit (WDK)
# This file provides the structure but actual driver builds typically use Visual Studio
# with WDK integration or the command-line build tools from WDK.

cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project(blackbird_driver VERSION 1.0 LANGUAGES C)

# This is a placeholder CMakeLists for the kernel driver
# Actual kernel driver compilation requires:
# 1. Windows Driver Kit (WDK) installed
# 2. Visual Studio with driver development support
# 3. Using MSBuild or the WDK command line tools

# For manual building, use the following command in a WDK command prompt:
# msbuild blackbird_driver.vcxproj /p:Configuration=Release /p:Platform=x64

# The driver source file
set(DRIVER_SOURCES
    driver.c
)

# Note: Kernel drivers cannot be built with standard CMake
# You must use Visual Studio with WDK or use the WDK build environment
message(STATUS "Kernel driver source: ${DRIVER_SOURCES}")
message(STATUS "To build the kernel driver, use Visual Studio with WDK installed")
message(STATUS "or use the WDK build environment with the provided .inf file")

# Create a custom target that shows the driver files
add_custom_target(blackbird_driver_info ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Kernel driver files are available but require WDK to build"
    SOURCES ${DRIVER_SOURCES}
)
