#TODO
macro(luajit_compile_definitions)
  foreach(TARGET ${ARGV})
    if(MSVC)
      #target_compile_definitions("${TARGET}" PRIVATE "_CRT_SECURE_NO_WARNINGS" "_CRT_STDIO_INLINE=__declspec(dllexport)__inline")
      target_compile_definitions("${TARGET}" PRIVATE "_CRT_SECURE_NO_WARNINGS")
    else()
      target_compile_definitions("${TARGET}" PRIVATE "_FILE_OFFSET_BITS=64" "_LARGEFILE_SOURCE")
    endif(MSVC)

    target_compile_definitions("${TARGET}" PRIVATE "LUAJIT_TARGET=LUAJIT_ARCH_${LUAJIT_ARCH}" "LUAJIT_OS=LUAJIT_OS_${LUAJIT_OS}" "LUAJIT_DISABLE_FFI" "LUAJIT_DISABLE_BUFFER" "LUAJIT_DISABLE_PROFILE")
  endforeach()
endmacro()
