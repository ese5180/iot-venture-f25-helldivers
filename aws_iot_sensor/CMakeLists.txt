#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aws_iot)

# ================= APP sources ==================
target_sources(app PRIVATE src/main.c)
target_sources(app PRIVATE src/sensor/sensor.c)
target_sources(app PRIVATE src/json_payload/json_payload.c)
target_sources(app PRIVATE src/horse_payload/horse_payload.c)
target_sources(app PRIVATE src/cert_provision.c)

# ================= GNSS =========================
zephyr_library_sources(src/gnss/gnss_task.c)

# ================= includes ======================
zephyr_include_directories(src)
zephyr_include_directories(src/sensor)
zephyr_include_directories(src/json_payload)
zephyr_include_directories(src/horse_payload)
zephyr_include_directories(src/gnss)

zephyr_library_sources_ifdef(CONFIG_GNSS_SAMPLE_ASSISTANCE_MINIMAL src/gnss/assistance_minimal.c)
zephyr_library_sources_ifdef(CONFIG_GNSS_SAMPLE_ASSISTANCE_MINIMAL src/gnss/mcc_location_table.c)
