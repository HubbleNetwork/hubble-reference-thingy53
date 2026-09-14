# Copyright (c) 2024 HubbleNetwork
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

if (CONFIG_APP_USE_PASS_PREDICTION)
  if("$ENV{HUBBLE_API_TOKEN}" STREQUAL "")
    message(FATAL_ERROR "HUBBLE_API_TOKEN environment variable must be set")
  endif()

  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/sat_params.c
    COMMAND ${Python3_EXECUTABLE} ${ZEPHYR_HUBBLENETWORK_SDK_MODULE_DIR}/tools/orbital_params_fetch.py
    ${CMAKE_CURRENT_SOURCE_DIR}/src/
    DEPENDS ${ZEPHYR_HUBBLENETWORK_SDK_MODULE_DIR}/tools/orbital_params_fetch.py
    COMMENT "Generate EPP files needed for pass prediction"
  )
  add_custom_target(gen_epp DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/sat_params.c)

  # main.c #includes the generated sat_params.c, so the app target must not be
  # compiled until generation has run.
  add_dependencies(app gen_epp)
endif()

target_sources(app PRIVATE src/ble.c)
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/nvs.c)
target_sources(app PRIVATE src/sat_board.c)
target_sources(app PRIVATE src/sensor.c)
target_sources(app PRIVATE src/shell.c)
target_sources(app PRIVATE src/button.c)

target_sources_ifdef(CONFIG_APP_BLINK_LED app PRIVATE src/led.c)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)

include(ExternalProject)
