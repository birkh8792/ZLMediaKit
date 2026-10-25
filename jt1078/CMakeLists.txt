# MIT License
#
# Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
#
# JT/T 1078 独立源码包：文件集中于 jt1078/，编译进 zlmediakit，避免与 MediaKit 循环链接。

if(NOT ENABLE_RTPPROXY)
  message(WARNING "ENABLE_JT1078 requires ENABLE_RTPPROXY; JT1078 disabled")
  return()
endif()

file(GLOB JT1078_SRC_LIST
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

target_sources(zlmediakit PRIVATE ${JT1078_SRC_LIST})
target_include_directories(zlmediakit
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_compile_definitions(zlmediakit PUBLIC ENABLE_JT1078)
update_cached_list(MK_COMPILE_DEFINITIONS ENABLE_JT1078)

message(STATUS "JT/T 1078 功能已开启 (jt1078/)")
