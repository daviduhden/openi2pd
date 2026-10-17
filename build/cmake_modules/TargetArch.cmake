# Copyright (c) 2017-2023, The PurpleI2P Project
# This file is part of Open I2Pd and licensed under BSD3
# See full license text in LICENSE file at top of project tree

# Based on the Qt 5 processor detection code, so should be very accurate
# https://github.com/qt/qtbase/blob/dev/src/corelib/global/qprocessordetection.h
# Currently handles arm (v5, v6, v7, v8), x86 (32/64), ia64, mips (32/64, mipsel, mips64el) and ppc (32/64)

# Regarding POWER/PowerPC, just as is noted in the Qt source,
# "There are many more known variants/revisions that we do not handle/detect."

set(archdetect_c_code "
#if defined(__arm__) || defined(__TARGET_ARCH_ARM)|| defined(_M_ARM) || defined(_M_ARM64) || defined(__aarch64__) || defined(__ARM64__)
    #if defined(__ARM64_ARCH_8__) \\
        || defined(__aarch64__) \\
        || defined(__ARMv8__) \\
        || defined(__ARMv8_A__) \\
        || defined(_M_ARM64) \\
        || (defined(__TARGET_ARCH_ARM) && __TARGET_ARCH_ARM-0 >= 8)
        #error cmake_ARCH arm64
    #elif defined(__ARM_ARCH_7__) \\
        || defined(__ARM_ARCH_7A__) \\
        || defined(__ARM_ARCH_7R__) \\
        || defined(__ARM_ARCH_7M__) \\
        || defined(__ARM_ARCH_7S__) \\
        || defined(_ARM_ARCH_7) \\
        || defined(__CORE_CORTEXA__) \\
        || (defined(__TARGET_ARCH_ARM) && __TARGET_ARCH_ARM-0 >= 7)
        #error cmake_ARCH armv7
    #elif defined(__ARM_ARCH_6__) \\
        || defined(__ARM_ARCH_6J__) \\
        || defined(__ARM_ARCH_6T2__) \\
        || defined(__ARM_ARCH_6Z__) \\
        || defined(__ARM_ARCH_6K__) \\
        || defined(__ARM_ARCH_6ZK__) \\
        || defined(__ARM_ARCH_6M__) \\
        || (defined(__TARGET_ARCH_ARM) && __TARGET_ARCH_ARM-0 >= 6)
        #error cmake_ARCH armv6
    #elif defined(__ARM_ARCH_5TEJ__) \\
        || defined(__ARM_ARCH_5TE__) \\
        || (defined(__TARGET_ARCH_ARM) && __TARGET_ARCH_ARM-0 >= 5)
        #error cmake_ARCH armv5
    #else
        #error cmake_ARCH arm
    #endif
#elif defined(__i386) || defined(__i386__) || defined(_M_IX86)
    #error cmake_ARCH i386
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(_M_X64)
    #error cmake_ARCH x86_64
#elif defined(__ia64) || defined(__ia64__) || defined(_M_IA64)
    #error cmake_ARCH ia64
#elif defined(__mips) || defined(__mips__) || defined(_M_MRX000)
    #if defined(_MIPS_ARCH_MIPS64) || defined(__mips64)
        #if defined(__MIPSEL__)
            #error cmake_ARCH mips64el
        #else
            #error cmake_ARCH mips64
        #endif
    #elif defined(__MIPSEL__)
        #error cmake_ARCH mipsel
    #else
        #error cmake_ARCH mips
    #endif
#elif defined(__ppc__) || defined(__ppc) || defined(__powerpc__) || defined(__POWERPC__) \\
      || defined(_ARCH_COM) || defined(_ARCH_PWR) || defined(_ARCH_PPC)  \\
      || defined(_M_MPPC) || defined(_M_PPC)
    #if defined(__ppc64__) || defined(__powerpc64__) || defined(__64BIT__)
        #error cmake_ARCH ppc64
    #else
        #error cmake_ARCH ppc
    #endif
#endif

#error cmake_ARCH unknown
")

# Read the target architecture from compiler diagnostics, without executing code.
function(target_architecture output_var)
    file(WRITE "${CMAKE_BINARY_DIR}/arch.c" "${archdetect_c_code}")
    try_compile(compile_result_unused
        "${CMAKE_BINARY_DIR}"
        "${CMAKE_BINARY_DIR}/arch.c"
        OUTPUT_VARIABLE ARCH)
    string(REGEX MATCH "cmake_ARCH ([a-zA-Z0-9_]+)" ARCH "${ARCH}")
    string(REPLACE "cmake_ARCH " "" ARCH "${ARCH}")
    if(NOT ARCH)
        set(ARCH unknown)
    endif()
    set(${output_var} "${ARCH}" PARENT_SCOPE)
endfunction()
