# Copyright (c) 2024 HubbleNetwork
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

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
