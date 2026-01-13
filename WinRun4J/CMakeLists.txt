cmake_minimum_required(VERSION 3.20)
project(WinRun4J LANGUAGES C CXX)

# set(CMAKE_VERBOSE_MAKEFILE ON)

# Completely replace MSVC's default exception flags
set(CMAKE_CXX_FLAGS "/DWIN32 /D_WINDOWS /EHs- /EHc-" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS   "/DWIN32 /D_WINDOWS /EHs- /EHc-" CACHE STRING "" FORCE)

set(CMAKE_CXX_FLAGS_RELEASE "/EHs- /EHc-" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG   "/EHs- /EHc-" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/EHs- /EHc-" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_MINSIZEREL "/EHs- /EHc-" CACHE STRING "" FORCE)

# ------------------------------------------------------------
# Global flags
# ------------------------------------------------------------
include(cmake/flags-common.cmake)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    include(cmake/flags-win32.cmake)
endif()

# ------------------------------------------------------------
# Output directories
# ------------------------------------------------------------
set(OUTDIR ${CMAKE_SOURCE_DIR}/build/${PROJECT_NAME}-${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTDIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTDIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTDIR})

# ------------------------------------------------------------
# Source groups
# ------------------------------------------------------------
set(LAUNCHER_COMMON
    src/WinRun4J.cpp
    src/WinRun4J.h
    src/WinRun4J.rc

    src/common/Dictionary.cpp
    src/common/Icon.cpp
    src/common/INI.cpp
    src/common/Log.cpp
    src/common/Registry.cpp
    src/common/Resource.cpp
    src/common/Runtime.cpp

    src/java/Classpath.cpp
    src/java/JNI.cpp
    src/java/VM.cpp

    src/launcher/DDE.cpp
    src/launcher/EventLog.cpp
    src/launcher/Native.cpp
    src/launcher/Service.cpp
    src/launcher/Shell.cpp
    src/launcher/SplashScreen.cpp
    src/launcher/Supervisor.cpp

    src/libffi/ffi.c
    src/libffi/prep_cif.c
    src/libffi/types.c
    src/libffi/closures.c
)

set(RCEDIT_COMMON
    src/ResourceEditor.cpp
    src/ResourceEditor.h

    src/common/Dictionary.cpp
    src/common/INI.cpp
    src/common/Log.cpp
    src/common/Resource.cpp
    src/common/Runtime.cpp
)

# ------------------------------------------------------------
# libffi assembly (serialized, MASM via asm.bat)
# ------------------------------------------------------------
enable_language(ASM_MASM)

# Select arch-specific sources and outputs
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(FFI_ASM_SRC ${CMAKE_SOURCE_DIR}/src/libffi/win32.S)
    set(FFI_ASM_BAT ${CMAKE_SOURCE_DIR}/src/libffi/asm.bat)
    set(FFI_ASM_OBJ ${CMAKE_BINARY_DIR}/win32.obj)
    set(FFI_ARCH win32)
else()
    set(FFI_ASM_SRC ${CMAKE_SOURCE_DIR}/src/libffi/win64.S)
    set(FFI_ASM_BAT ${CMAKE_SOURCE_DIR}/src/libffi/asm64.bat)
    set(FFI_ASM_OBJ ${CMAKE_BINARY_DIR}/win64.obj)
    set(FFI_ARCH win64)
endif()

# Define a job pool that only allows 1 job at a time for libffi asm
# This prevents concurrent asm.bat/ml.exe runs which corrupt win32.asm/obj
set_property(GLOBAL PROPERTY JOB_POOLS "ffi_asm_pool=1")

# Custom command: runs your asm.bat to generate the .obj file
add_custom_command(
    OUTPUT ${FFI_ASM_OBJ}
    COMMAND ${FFI_ASM_BAT}
            ${CMAKE_SOURCE_DIR}/src/libffi
            ${FFI_ARCH}
            ${CMAKE_BINARY_DIR}
    DEPENDS ${FFI_ASM_SRC}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/libffi
    COMMENT "Assembling libffi ${FFI_ARCH} backend"
)

# Custom target that represents "the libffi asm object exists"
add_custom_target(ffi_asm ALL
    DEPENDS ${FFI_ASM_OBJ}
)

# Force this target (and thus the asm.bat/ml.exe step) into the serial pool
set_property(TARGET ffi_asm PROPERTY JOB_POOL_COMPILE ffi_asm_pool)
set_property(TARGET ffi_asm PROPERTY JOB_POOL_LINK    ffi_asm_pool)

# ------------------------------------------------------------
# Helper: create launcher variants
# ------------------------------------------------------------
function(add_launcher name subsystem)
    add_executable(${name} ${LAUNCHER_COMMON} ${FFI_ASM_OBJ})

    target_include_directories(${name} PRIVATE
        ${JAVA_HOME}/include
        ${JAVA_HOME}/include/win32
    )

    target_link_libraries(${name} PRIVATE
        psapi.lib user32.lib ole32.lib olepro32.lib advapi32.lib gdi32.lib
    )

    target_link_options(${name} PRIVATE /SUBSYSTEM:${subsystem})

    if(${subsystem} STREQUAL "CONSOLE")
        target_compile_definitions(${name} PRIVATE CONSOLE)
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_link_options(${name} PRIVATE /SAFESEH:NO)
        target_compile_definitions(${name} PRIVATE X86_WIN32)
    else()
        target_compile_definitions(${name} PRIVATE X86_WIN64)
    endif()
    
	target_sources(${name} PRIVATE ${FFI_ASM_OBJ})
	add_dependencies(${name} ffi_asm)    
endfunction()


# ------------------------------------------------------------
# Helper: create rcedit variants
# ------------------------------------------------------------
function(add_rcedit name)
    add_executable(${name} ${RCEDIT_COMMON})

    target_link_libraries(${name} PRIVATE
        user32.lib gdi32.lib comdlg32.lib shell32.lib
    )

    target_link_options(${name} PRIVATE /SUBSYSTEM:CONSOLE)
endfunction()

# ------------------------------------------------------------
# Build all six executables
# ------------------------------------------------------------
# Win32
add_launcher(WinRun4J WINDOWS)
add_launcher(WinRun4Jc CONSOLE)
add_rcedit(RCEDIT)

# x64
add_launcher(WinRun4J64 WINDOWS)
add_launcher(WinRun4J64c CONSOLE)
add_rcedit(RCEDIT64)

