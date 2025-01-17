cmake_minimum_required(VERSION 3.20)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(LUAJIT_SUBPROJECT ON)
else()
  set(LUAJIT_SUBPROJECT OFF)
endif()

set(LUAJIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../luajit")

if(LUAJIT_SUBPROJECT)
  project(luajit-minilua)
endif()

add_executable(minilua "${LUAJIT_DIR}/src/host/minilua.c")

if(MSVC)
  target_compile_definitions(minilua PRIVATE "_CRT_SECURE_NO_WARNINGS")
endif(MSVC)

if(LUAJIT_SUBPROJECT)
  install(TARGETS minilua)
else()
  set_property(TARGET minilua PROPERTY FOLDER "External Libraries")
endif()
