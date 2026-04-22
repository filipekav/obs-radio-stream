cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

set(ENABLE_FRONTEND_API ON)
set(ENABLE_QT ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Network REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

find_package(mp3lame CONFIG)
if(mp3lame_FOUND)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE mp3lame::mp3lame)
else()
  find_path(LAME_INCLUDE_DIR NAMES lame/lame.h)
  find_library(
    LAME_LIBRARY
    NAMES mp3lame lame libmp3lame libmp3lame-static mp3lame-static
  )
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${LAME_INCLUDE_DIR})
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${LAME_LIBRARY})
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.cpp src/radio-output.cpp src/radio-streamer.cpp src/radio-ui.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(WIN32)
  # Lista explícita das DLLs de terceiros que desejamos empacotar.
  # Descomente e ajuste a linha do libshout com o nome exato do target que você usa.
  set(
    EXTERNAL_RUNTIME_DLLS
    "$<$<TARGET_EXISTS:mp3lame::mp3lame>:$<TARGET_FILE:mp3lame::mp3lame>>"
    # "$<$<TARGET_EXISTS:shout::shout>:$<TARGET_FILE:shout::shout>>"
  )

  # 1. POST_BUILD: Copia apenas as DLLs especificadas para a pasta local de saída
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${EXTERNAL_RUNTIME_DLLS} "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>"
    COMMAND_EXPAND_LISTS
  )

  # 2. INSTALL: Empacota especificamente e apenas as DLLs externas no ZIP
  install(FILES ${EXTERNAL_RUNTIME_DLLS} DESTINATION "obs-plugins/64bit")
endif()
