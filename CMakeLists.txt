# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Set the target chip
set(IDF_TARGET esp32c3)

# ESP-IDF minimum version
set(SUPPORTED_TARGETS esp32c3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Maker_Chip_Gacha)
