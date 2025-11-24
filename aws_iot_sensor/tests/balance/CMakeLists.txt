# tests/balance/CMakeLists.txt
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(horse_balance_test)

# 只拉进来要测试的逻辑模块 + 本目录的测试代码
target_sources(app PRIVATE
  ../../src/sensor/horse_balance.c
  src/balance_test.c
)

# 让测试代码能找到 horse_balance.h
target_include_directories(app PRIVATE
  ../../src/sensor
)