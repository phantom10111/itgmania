cmake_minimum_required(VERSION 3.20)
project(luajit-minilua)

set(LUAJIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../luajit")

add_executable(minilua "${LUAJIT_DIR}/src/host/minilua.c")

if(MSVC)
  target_compile_definitions(minilua PRIVATE "_CRT_SECURE_NO_WARNINGS")
endif(MSVC)

install(TARGETS minilua)
