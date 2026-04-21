cmake_minimum_required(VERSION 3.16...3.24)
project(obs-radio-stream VERSION 1.0.0)

# Set up Qt GUI specifics
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

# Find OBS
find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)

# LAME and SHOUT config
# Allows fallback configuration path
find_path(LAME_INCLUDE_DIR NAMES lame/lame.h)
find_library(LAME_LIBRARY NAMES mp3lame lame)

find_path(SHOUT_INCLUDE_DIR NAMES shout/shout.h)
find_library(SHOUT_LIBRARY NAMES shout)

if (NOT LAME_INCLUDE_DIR OR NOT LAME_LIBRARY)
    message(WARNING "LAME not found automatically. Make sure to specify -DCMAKE_PREFIX_PATH or set LAME_INCLUDE_DIR and LAME_LIBRARY directly.")
endif()

if (NOT SHOUT_INCLUDE_DIR OR NOT SHOUT_LIBRARY)
    message(WARNING "libshout not found automatically. Make sure to specify -DCMAKE_PREFIX_PATH or set SHOUT_INCLUDE_DIR and SHOUT_LIBRARY directly.")
endif()

add_library(${PROJECT_NAME} MODULE
    plugin-main.cpp
    radio-output.cpp
    radio-streamer.cpp
    radio-ui.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

target_include_directories(${PROJECT_NAME} PRIVATE
    ${LAME_INCLUDE_DIR}
    ${SHOUT_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
    ${LAME_LIBRARY}
    ${SHOUT_LIBRARY}
)
