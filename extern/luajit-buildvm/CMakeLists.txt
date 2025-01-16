cmake_minimum_required(VERSION 3.20)
project(luajit-buildvm)

include(../luajit-cmake/luajit-compile-definitions.cmake)

set(LUAJIT_ARCH "" CACHE STRING "Target arch for luajit")
set(LUAJIT_OS "" CACHE STRING "Target OS for luajit")
set(LUAJIT_GENERATED_INCLUDE_DIR "" CACHE PATH "luajit generated headers directory")
set(LUAJIT_MINILUA "" CACHE FILEPATH "luajit minilua executable")

set(LUAJIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../luajit")

add_executable(minilua IMPORTED)
set_property(TARGET minilua PROPERTY IMPORTED_LOCATION "${LUAJIT_MINILUA}")

set(LUAJIT_GENERATED_HOST_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/host")

set(LUAJIT_DYNASM_DASC "${LUAJIT_DIR}/src/vm_${LUAJIT_ARCH}.dasc")

#TODO
if("${LUAJIT_OS}" EQUAL "WINDOWS")
  list(APPEND LUAJIT_DASM_FLAGS -D WIN -D JIT -D ENDIAN_LE -D FPU -D P64)
else()
  list(APPEND LUAJIT_DASM_FLAGS -D JIT -D ENDIAN_LE -D FPU -D P64)
endif()

add_custom_command(
  OUTPUT "${LUAJIT_GENERATED_HOST_INCLUDE_DIR}/buildvm_arch.h"
  COMMAND minilua ARGS "${LUAJIT_DIR}/dynasm/dynasm.lua" -LN ${LUAJIT_DASM_FLAGS} -o "${LUAJIT_GENERATED_HOST_INCLUDE_DIR}/buildvm_arch.h" "${LUAJIT_DYNASM_DASC}"
  DEPENDS
    minilua
    "${LUAJIT_DIR}/dynasm/dynasm.lua"
    "${LUAJIT_DYNASM_DASC}"
    "${LUAJIT_DIR}/src/lj_arch.h"
    "${LUAJIT_DIR}/src/lua.h"
    "${LUAJIT_DIR}/src/luaconf.h")

add_custom_target(generate_buildvm_files DEPENDS "${LUAJIT_GENERATED_HOST_INCLUDE_DIR}/buildvm_arch.h")

set(LUAJIT_BUILDVM_SRC
  "${LUAJIT_DIR}/src/host/buildvm.c"
  "${LUAJIT_DIR}/src/host/buildvm_asm.c"
  "${LUAJIT_DIR}/src/host/buildvm_fold.c"
  "${LUAJIT_DIR}/src/host/buildvm_lib.c"
  "${LUAJIT_DIR}/src/host/buildvm_peobj.c")

add_executable(buildvm ${LUAJIT_BUILDVM_SRC})
add_dependencies(buildvm generate_buildvm_files)

target_include_directories(buildvm PRIVATE "${LUAJIT_DIR}/src" "${LUAJIT_GENERATED_INCLUDE_DIR}" "${LUAJIT_GENERATED_HOST_INCLUDE_DIR}")

luajit_compile_definitions(buildvm)

install(TARGETS buildvm)
