include(ExternalProject)
include(luajit-buildvm/luajit-compile-definitions.cmake)

#TODO
if(MSVC)
    set(LUAJIT_ARCH "x64")
    set(LUAJIT_OS "WINDOWS")
else()
    set(LUAJIT_ARCH "x64")
    set(LUAJIT_OS "LINUX")
endif()

set(LUAJIT_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/luajit/generated")

configure_file("luajit-buildvm/luajit_relver.txt.in" "${LUAJIT_GENERATED_INCLUDE_DIR}/luajit_relver.txt")

ExternalProject_Add(
    luajit_buildvm_project
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/luajit-buildvm"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/luajit-buildvm/build"
    INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/luajit-buildvm/install"
    BUILD_IN_SOURCE OFF
    CMAKE_ARGS
        "-DCMAKE_SYSTEM_NAME=${DCMAKE_HOST_SYSTEM_NAME}"
        "-DCMAKE_SYSTEM_PROCESSOR=${DCMAKE_HOST_SYSTEM_PROCESSOR}"
        "-DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/luajit-buildvm/install"
        "-DLUAJIT_ARCH=${LUAJIT_ARCH}"
        "-DLUAJIT_OS=${LUAJIT_OS}"
        "-DLUAJIT_RELVER_FILE=${LUAJIT_GENERATED_INCLUDE_DIR}/luajit_relver.txt"
)

add_executable(minilua IMPORTED)
set_property(TARGET minilua PROPERTY IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/luajit-buildvm/install/bin/minilua${CMAKE_EXECUTABLE_SUFFIX}")
add_dependencies(minilua luajit_buildvm_project)

add_executable(buildvm IMPORTED)
set_property(TARGET buildvm PROPERTY IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/luajit-buildvm/install/bin/buildvm${CMAKE_EXECUTABLE_SUFFIX}")
add_dependencies(buildvm luajit_buildvm_project)

add_custom_command(
    OUTPUT "${LUAJIT_GENERATED_INCLUDE_DIR}/luajit.h"
    COMMAND minilua
    ARGS
        "luajit/src/host/genversion.lua"
        "luajit/src/luajit_rolling.h"
        "${LUAJIT_GENERATED_INCLUDE_DIR}/luajit_relver.txt"
        "${LUAJIT_GENERATED_INCLUDE_DIR}/luajit.h"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS
        minilua
        "luajit/src/host/genversion.lua"
        "luajit/src/luajit_rolling.h"
        "${LUAJIT_GENERATED_INCLUDE_DIR}/luajit_relver.txt")

set(LUAJIT_LIB_SRC
    "luajit/src/lib_base.c"
    "luajit/src/lib_math.c"
    "luajit/src/lib_bit.c"
    "luajit/src/lib_string.c"
    "luajit/src/lib_table.c"
    "luajit/src/lib_io.c"
    "luajit/src/lib_os.c"
    "luajit/src/lib_package.c"
    "luajit/src/lib_debug.c"
    "luajit/src/lib_jit.c"
    "luajit/src/lib_ffi.c"
    "luajit/src/lib_buffer.c")

add_custom_command(
    OUTPUT "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_bcdef.h"
    COMMAND buildvm ARGS -m bcdef -o "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_bcdef.h" ${LUAJIT_LIB_SRC}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS buildvm ${LUAJIT_LIB_SRC})

add_custom_command(
    OUTPUT "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_ffdef.h"
    COMMAND buildvm ARGS -m ffdef -o "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_ffdef.h" ${LUAJIT_LIB_SRC}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS buildvm ${LUAJIT_LIB_SRC})

add_custom_command(
    OUTPUT "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_libdef.h"
    COMMAND buildvm ARGS -m libdef -o "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_libdef.h" ${LUAJIT_LIB_SRC}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS buildvm ${LUAJIT_LIB_SRC})

add_custom_command(
    OUTPUT "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_recdef.h"
    COMMAND buildvm ARGS -m recdef -o "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_recdef.h" ${LUAJIT_LIB_SRC}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS buildvm ${LUAJIT_LIB_SRC})

add_custom_command(
    OUTPUT "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_folddef.h"
    COMMAND buildvm ARGS -m folddef -o "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_folddef.h" "luajit/src/lj_opt_fold.c"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    DEPENDS buildvm ${LUAJIT_LIB_SRC})

add_custom_target(generate_luajit_files DEPENDS
    "${LUAJIT_GENERATED_INCLUDE_DIR}/luajit.h"
    "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_bcdef.h"
    "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_ffdef.h"
    "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_libdef.h"
    "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_recdef.h"
    "${LUAJIT_GENERATED_INCLUDE_DIR}/lj_folddef.h")

set(LUAJIT_SRC
    "luajit/src/lj_alloc.c"
    "luajit/src/lj_api.c"
    "luajit/src/lj_asm.c"
    "luajit/src/lj_assert.c"
    "luajit/src/lj_bc.c"
    "luajit/src/lj_bcread.c"
    "luajit/src/lj_bcwrite.c"
    "luajit/src/lj_buf.c"
    "luajit/src/lj_carith.c"
    "luajit/src/lj_ccall.c"
    "luajit/src/lj_ccallback.c"
    "luajit/src/lj_cconv.c"
    "luajit/src/lj_cdata.c"
    "luajit/src/lj_char.c"
    "luajit/src/lj_clib.c"
    "luajit/src/lj_cparse.c"
    "luajit/src/lj_crecord.c"
    "luajit/src/lj_ctype.c"
    "luajit/src/lj_debug.c"
    "luajit/src/lj_dispatch.c"
    "luajit/src/lj_err.c"
    "luajit/src/lj_ffrecord.c"
    "luajit/src/lj_func.c"
    "luajit/src/lj_gc.c"
    "luajit/src/lj_gdbjit.c"
    "luajit/src/lj_ir.c"
    "luajit/src/lj_lex.c"
    "luajit/src/lj_lib.c"
    "luajit/src/lj_load.c"
    "luajit/src/lj_mcode.c"
    "luajit/src/lj_meta.c"
    "luajit/src/lj_obj.c"
    "luajit/src/lj_opt_dce.c"
    "luajit/src/lj_opt_fold.c"
    "luajit/src/lj_opt_loop.c"
    "luajit/src/lj_opt_mem.c"
    "luajit/src/lj_opt_narrow.c"
    "luajit/src/lj_opt_sink.c"
    "luajit/src/lj_opt_split.c"
    "luajit/src/lj_parse.c"
    "luajit/src/lj_prng.c"
    "luajit/src/lj_profile.c"
    "luajit/src/lj_record.c"
    "luajit/src/lj_serialize.c"
    "luajit/src/lj_snap.c"
    "luajit/src/lj_state.c"
    "luajit/src/lj_str.c"
    "luajit/src/lj_strfmt.c"
    "luajit/src/lj_strfmt_num.c"
    "luajit/src/lj_strscan.c"
    "luajit/src/lj_tab.c"
    "luajit/src/lj_trace.c"
    "luajit/src/lj_udata.c"
    "luajit/src/lj_vmevent.c"
    "luajit/src/lj_vmmath.c"
    "luajit/src/lib_aux.c"
    "luajit/src/lib_init.c"
	${LUAJIT_LIB_SRC})

set(LUAJIT_H
    "luajit/src/lua.h"
    "luajit/src/lualib.h"
    "luajit/src/lauxlib.h"
    "luajit/src/lj_alloc.h"
    "luajit/src/lj_arch.h"
    "luajit/src/lj_asm.h"
    "luajit/src/lj_asm_arm.h"
    "luajit/src/lj_asm_arm64.h"
    "luajit/src/lj_asm_mips.h"
    "luajit/src/lj_asm_ppc.h"
    "luajit/src/lj_asm_x86.h"
    "luajit/src/lj_bc.h"
    "luajit/src/lj_bcdump.h"
    "luajit/src/lj_buf.h"
    "luajit/src/lj_carith.h"
    "luajit/src/lj_ccall.h"
    "luajit/src/lj_ccallback.h"
    "luajit/src/lj_cconv.h"
    "luajit/src/lj_cdata.h"
    "luajit/src/lj_char.h"
    "luajit/src/lj_clib.h"
    "luajit/src/lj_cparse.h"
    "luajit/src/lj_crecord.h"
    "luajit/src/lj_ctype.h"
    "luajit/src/lj_debug.h"
    "luajit/src/lj_def.h"
    "luajit/src/lj_dispatch.h"
    "luajit/src/lj_emit_arm.h"
    "luajit/src/lj_emit_arm64.h"
    "luajit/src/lj_emit_mips.h"
    "luajit/src/lj_emit_ppc.h"
    "luajit/src/lj_emit_x86.h"
    "luajit/src/lj_err.h"
    "luajit/src/lj_errmsg.h"
    "luajit/src/lj_ff.h"
    "luajit/src/lj_ffrecord.h"
    "luajit/src/lj_frame.h"
    "luajit/src/lj_func.h"
    "luajit/src/lj_gc.h"
    "luajit/src/lj_gdbjit.h"
    "luajit/src/lj_ir.h"
    "luajit/src/lj_ircall.h"
    "luajit/src/lj_iropt.h"
    "luajit/src/lj_jit.h"
    "luajit/src/lj_lex.h"
    "luajit/src/lj_lib.h"
    "luajit/src/lj_mcode.h"
    "luajit/src/lj_meta.h"
    "luajit/src/lj_obj.h"
    "luajit/src/lj_parse.h"
    "luajit/src/lj_prng.h"
    "luajit/src/lj_profile.h"
    "luajit/src/lj_record.h"
    "luajit/src/lj_serialize.h"
    "luajit/src/lj_snap.h"
    "luajit/src/lj_state.h"
    "luajit/src/lj_str.h"
    "luajit/src/lj_strfmt.h"
    "luajit/src/lj_strscan.h"
    "luajit/src/lj_tab.h"
    "luajit/src/lj_target.h"
    "luajit/src/lj_target_arm.h"
    "luajit/src/lj_target_arm64.h"
    "luajit/src/lj_target_mips.h"
    "luajit/src/lj_target_ppc.h"
    "luajit/src/lj_target_x86.h"
    "luajit/src/lj_trace.h"
    "luajit/src/lj_traceerr.h"
    "luajit/src/lj_udata.h"
    "luajit/src/lj_vm.h"
    "luajit/src/lj_vmevent.h")

source_group("" FILES ${LUAJIT_SRC})
source_group("" FILES ${LUAJIT_H})

add_library(lua-5.1 STATIC ${LUAJIT_SRC} ${LUAJIT_H})
add_dependencies(lua-5.1 generate_luajit_files)

set_property(TARGET lua-5.1 PROPERTY FOLDER "External Libraries")

target_include_directories(lua-5.1 PRIVATE "${LUAJIT_GENERATED_INCLUDE_DIR}")
target_include_directories(lua-5.1 INTERFACE "luajit/src")

luajit_compile_definitions(lua-5.1)

disable_project_warnings(lua-5.1)
