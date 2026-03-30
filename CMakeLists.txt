cmake_minimum_required(VERSION 3.18.0)
project(n0s-ryo-miner)

################################################################################
# C++ Standard & Compiler Requirements
################################################################################

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# GCC 7+ required for C++17 support
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0)
        message(FATAL_ERROR "g++ version must be at least 7.0 for C++17 support!")
    endif()
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE PATH "install prefix" FORCE)
endif()

# Help find CUDA/OpenCL from environment
list(APPEND CMAKE_PREFIX_PATH "$ENV{CUDA_ROOT}" "$ENV{OCL_ROOT}" "$ENV{AMDAPPSDKROOT}" "$ENV{CMAKE_PREFIX_PATH}")

################################################################################
# Build Options
################################################################################

set(BUILD_TYPE "Release;Debug")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${BUILD_TYPE}")

set(N0S_COMPILE "native" CACHE STRING "Select CPU compile mode (native or generic)")
set_property(CACHE N0S_COMPILE PROPERTY STRINGS "native;generic")
if(N0S_COMPILE STREQUAL "native")
    set(CMAKE_CXX_FLAGS "-march=native -mtune=native ${CMAKE_CXX_FLAGS}")
    set(CMAKE_C_FLAGS "-march=native -mtune=native ${CMAKE_C_FLAGS}")
elseif(N0S_COMPILE STREQUAL "generic")
    add_definitions("-DCONF_ENFORCE_OpenCL_1_2=1")
else()
    message(FATAL_ERROR "N0S_COMPILE is set to an unknown value '${N0S_COMPILE}'")
endif()

option(CMAKE_LINK_STATIC "Link as much as possible libraries static" OFF)

################################################################################
# Find CUDA (NVIDIA Backend)
################################################################################

option(CUDA_ENABLE "Enable NVIDIA CUDA backend" ON)
if(CUDA_ENABLE)
    find_package(CUDA 11.0)

    if(CUDA_FOUND)
        # Find libcuda (driver API)
        find_library(CUDA_LIB
            NAMES libcuda cuda
            HINTS ${CUDA_TOOLKIT_ROOT_DIR} /usr /usr/local/cuda
            PATH_SUFFIXES lib64 lib64/stubs lib)

        # Find nvrtc (runtime compilation)
        find_library(CUDA_NVRTC_LIB
            NAMES libnvrtc nvrtc
            HINTS ${CUDA_TOOLKIT_ROOT_DIR} /usr /usr/local/cuda
            PATH_SUFFIXES lib64 lib)

        list(APPEND BACKEND_TYPES "nvidia")

        option(N0S_LARGEGRID "Support large CUDA block count > 128" ON)
        if(N0S_LARGEGRID)
            add_definitions("-DN0S_LARGEGRID=${N0S_LARGEGRID}")
        endif()

        # GPU Architecture Targets
        # Minimum: Pascal (sm_61). Maximum depends on CUDA version.
        set(DEFAULT_CUDA_ARCH "61;75;80;86")  # Pascal, Turing, Ampere

        if(NOT CUDA_VERSION VERSION_LESS 11.8)
            list(APPEND DEFAULT_CUDA_ARCH "89")  # Ada Lovelace (RTX 40xx)
        endif()
        if(NOT CUDA_VERSION VERSION_LESS 12.0)
            list(APPEND DEFAULT_CUDA_ARCH "90")  # Hopper (H100)
        endif()
        if(NOT CUDA_VERSION VERSION_LESS 12.8)
            list(APPEND DEFAULT_CUDA_ARCH "100;120")  # Blackwell (RTX 50xx)
        endif()

        set(CUDA_ARCH "${DEFAULT_CUDA_ARCH}" CACHE STRING "GPU architectures (semicolon list, e.g. '61;75;86')")

        # Validate architectures
        foreach(CUDA_ARCH_ELEM ${CUDA_ARCH})
            string(REGEX MATCH "^[0-9]+$" IS_NUMBER ${CUDA_ARCH_ELEM})
            if(NOT IS_NUMBER)
                message(FATAL_ERROR "Invalid CUDA architecture '${CUDA_ARCH_ELEM}' — must be a number (e.g. '61')")
            endif()
            if(${CUDA_ARCH_ELEM} LESS 60)
                message(FATAL_ERROR "Unsupported CUDA architecture '${CUDA_ARCH_ELEM}'. Minimum is 60 (Pascal).")
            endif()
        endforeach()

        string(REPLACE ";" "+" STR_CUDA_ARCH "${CUDA_ARCH}")
        add_definitions("-DN0S_CUDA_ARCH_LIST=${STR_CUDA_ARCH}")

        # NVCC Flags
        # CRITICAL: --fmad=false --prec-div=true --ftz=false required for correct cn_gpu hashes
        set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} -std=c++17")
        set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} --fmad=false --prec-div=true --ftz=false")
        set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS} --default-stream per-thread")

        foreach(CUDA_ARCH_ELEM ${CUDA_ARCH})
            set(CUDA_NVCC_FLAGS ${CUDA_NVCC_FLAGS}
                "--generate-code arch=compute_${CUDA_ARCH_ELEM},code=sm_${CUDA_ARCH_ELEM}"
                "--generate-code arch=compute_${CUDA_ARCH_ELEM},code=compute_${CUDA_ARCH_ELEM}")
        endforeach()

        option(CUDA_SHOW_REGISTER "Show registers used per kernel" OFF)
        option(CUDA_SHOW_CODELINES "Show kernel lines in cuda-gdb" OFF)
        option(CUDA_KEEP_FILES "Keep intermediate compilation files" OFF)

        if(CUDA_SHOW_CODELINES)
            set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS}" --source-in-ptx -lineinfo)
            set(CUDA_KEEP_FILES ON CACHE BOOL "activate keep files" FORCE)
        endif()
        if(CUDA_SHOW_REGISTER)
            set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS}" -Xptxas=-v)
        endif()
        if(CUDA_KEEP_FILES)
            set(CUDA_NVCC_FLAGS "${CUDA_NVCC_FLAGS}" --keep --keep-dir "${PROJECT_BINARY_DIR}")
        endif()

        set(CUDA_HOST_COMPILER ${CMAKE_CXX_COMPILER} CACHE FILEPATH "Host compiler for NVCC" FORCE)
    else()
        message(FATAL_ERROR "CUDA NOT found: use `-DCUDA_ENABLE=OFF` to build without NVIDIA support")
    endif()
else()
    add_definitions("-DCONF_NO_CUDA")
endif()

################################################################################
# Find OpenCL (AMD Backend)
################################################################################

option(OpenCL_ENABLE "Enable AMD OpenCL backend" ON)
if(OpenCL_ENABLE)
    find_path(OpenCL_INCLUDE_DIR
        NAMES CL/cl.h OpenCL/cl.h
        NO_DEFAULT_PATH
        PATHS ENV "OCL_ROOT" ENV "OpenCL_ROOT" ENV AMDAPPSDKROOT /usr
        PATH_SUFFIXES include OpenCL/common/inc)

    find_library(OpenCL_LIBRARY
        NAMES OpenCL
        NO_DEFAULT_PATH
        PATHS ENV "OCL_ROOT" ENV "OpenCL_ROOT" ENV AMDAPPSDKROOT
        PATH_SUFFIXES lib/x86_64 lib/x86_64-linux-gnu lib64)

    find_package(OpenCL)
    if(OpenCL_FOUND)
        list(APPEND BACKEND_TYPES "amd")
        include_directories(SYSTEM ${OpenCL_INCLUDE_DIRS})
        link_directories(${OpenCL_LIBRARY})
    else()
        message(FATAL_ERROR "OpenCL NOT found: use `-DOpenCL_ENABLE=OFF` to build without AMD support")
    endif()
else()
    add_definitions("-DCONF_NO_OPENCL")
endif()

# CPU mining removed — shared crypto still compiled for GPU hash verification
add_definitions("-DCONF_NO_CPU")

# AVX2 for Clang
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(n0s/backend/cpu/crypto/cn_gpu_avx.cpp PROPERTIES COMPILE_FLAGS "-mavx2")
endif()

################################################################################
# Find Dependencies
################################################################################

# PThreads
find_package(Threads REQUIRED)
set(LIBS ${LIBS} ${CMAKE_THREAD_LIBS_INIT})

# microhttpd (HTTP monitoring API)
option(MICROHTTPD_ENABLE "Enable HTTP monitoring daemon" ON)
if(MICROHTTPD_ENABLE)
    find_path(MTHD_INCLUDE_DIR NAMES microhttpd.h PATHS /opt/local /usr/local /usr PATH_SUFFIXES include)
    find_library(MHTD NAMES microhttpd PATHS ENV "MICROHTTPD_ROOT" PATH_SUFFIXES lib)
    if(MHTD STREQUAL "MHTD-NOTFOUND")
        message(FATAL_ERROR "microhttpd NOT found: use `-DMICROHTTPD_ENABLE=OFF` to build without HTTP daemon")
    else()
        set(LIBS ${LIBS} ${MHTD})
        include_directories(AFTER ${MTHD_INCLUDE_DIR})
    endif()
else()
    add_definitions("-DCONF_NO_HTTPD")
endif()

# OpenSSL
option(OpenSSL_ENABLE "Enable OpenSSL (TLS pool connections)" ON)
if(OpenSSL_ENABLE)
    find_package(OpenSSL)
    if(OPENSSL_FOUND)
        include_directories(${OPENSSL_INCLUDE_DIR})
        set(LIBS ${LIBS} ${OPENSSL_LIBRARIES})
    else()
        message(FATAL_ERROR "OpenSSL NOT found: use `-DOpenSSL_ENABLE=OFF` to build without TLS")
    endif()
else()
    add_definitions("-DCONF_NO_TLS")
endif()

# hwloc (NUMA-aware memory allocation)
option(HWLOC_ENABLE "Enable hwloc for NUMA support" ON)
if(HWLOC_ENABLE)
    find_path(HWLOC_INCLUDE_DIR NAMES hwloc.h PATHS /opt/local /usr/local /usr PATH_SUFFIXES include)
    find_library(HWLOC NAMES hwloc PATHS ENV "HWLOC_ROOT" PATH_SUFFIXES lib)
    if(HWLOC STREQUAL "HWLOC-NOTFOUND" OR ${HWLOC_INCLUDE_DIR} STREQUAL "HWLOC_INCLUDE_DIR-NOTFOUND")
        message(FATAL_ERROR "hwloc NOT found: use `-DHWLOC_ENABLE=OFF` to build without NUMA support")
    else()
        set(LIBS ${LIBS} ${HWLOC})
        include_directories(AFTER ${HWLOC_INCLUDE_DIR})
    endif()
else()
    add_definitions("-DCONF_NO_HWLOC")
endif()

################################################################################
# Versioning
################################################################################

execute_process(COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)

if(GIT_COMMIT_HASH)
    add_definitions("-DGIT_COMMIT_HASH=${GIT_COMMIT_HASH}")
endif()
if(GIT_BRANCH)
    add_definitions("-DGIT_BRANCH=${GIT_BRANCH}")
endif()

string(REPLACE ";" "-" STR_BACKEND_TYPES "${BACKEND_TYPES}")
add_definitions("-DBACKEND_TYPE=${STR_BACKEND_TYPES}")

################################################################################
# Compile & Link
################################################################################

include_directories(BEFORE .)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2 -maes")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse2 -maes")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
    endif()
    set(CMAKE_CXX_FLAGS "-Wl,-z,noexecstack ${CMAKE_CXX_FLAGS}")
    set(CMAKE_C_FLAGS "-Wl,-z,noexecstack ${CMAKE_C_FLAGS}")
endif()

# Static linking option
if(CMAKE_LINK_STATIC)
    set(BUILD_SHARED_LIBRARIES OFF)
    set(DL_LIB ${CMAKE_DL_LIBS})
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(LIBS "-static-libgcc -static-libstdc++ ${LIBS}")
    endif()
endif()

# ASM library removed — cryptonight_v8 mainloop was only used by removed algorithms

# C crypto library
file(GLOB SRCFILES_C "n0s/backend/cpu/crypto/*.c")
add_library(n0s-c STATIC ${SRCFILES_C})
set_property(TARGET n0s-c PROPERTY C_STANDARD 99)
if(MICROHTTPD_ENABLE)
    target_link_libraries(n0s-c ${MHTD})
endif()
target_link_libraries(n0s-c ${LIBS})

# Core backend library
file(GLOB BACKEND_CPP
    "n0s/*.cpp"
    "n0s/backend/cpu/*.cpp"
    "n0s/backend/*.cpp"
    "n0s/backend/cpu/crypto/*.cpp"
    "n0s/http/*.cpp"
    "n0s/misc/*.cpp"
    "n0s/net/*.cpp")
add_library(n0s-backend STATIC ${BACKEND_CPP})
target_link_libraries(n0s-backend n0s-c ${CMAKE_DL_LIBS})

# CUDA backend (shared library, loaded via dlopen)
if(CUDA_FOUND)
    file(GLOB CUDASRCFILES
        "n0s/backend/nvidia/nvcc_code/*.cu"
        "n0s/backend/nvidia/*.cpp")

    cuda_add_library(n0s_cuda_backend SHARED ${CUDASRCFILES})

    set(CUDA_LIBRARIES ${CUDA_LIB} ${CUDA_LIBRARIES} ${CUDA_NVRTC_LIB})
    target_link_libraries(n0s_cuda_backend ${CUDA_LIBRARIES})
    target_link_libraries(n0s_cuda_backend n0s-backend)
endif()

# OpenCL backend (shared library, loaded via dlopen)
if(OpenCL_FOUND)
    file(GLOB OPENCLSRCFILES
        "n0s/backend/amd/amd_gpu/*.cpp"
        "n0s/backend/amd/*.cpp")
    add_library(n0s_opencl_backend SHARED ${OPENCLSRCFILES})
    target_link_libraries(n0s_opencl_backend ${OpenCL_LIBRARY})
    target_link_libraries(n0s_opencl_backend n0s-backend)
endif()

# Main executable
file(GLOB SRCFILES_CPP "n0s/cli/*.cpp")
set_source_files_properties(${SRCFILES_CPP} PROPERTIES LANGUAGE CXX)
add_executable(n0s-ryo-miner ${SRCFILES_CPP})

set(EXECUTABLE_OUTPUT_PATH "bin" CACHE STRING "Path to executables relative to ${CMAKE_INSTALL_PREFIX}")
set(LIBRARY_OUTPUT_PATH "bin" CACHE STRING "Path to libraries relative to ${CMAKE_INSTALL_PREFIX}")

target_link_libraries(n0s-ryo-miner ${LIBS} n0s-c n0s-backend)

################################################################################
# Install
################################################################################

if(NOT CMAKE_INSTALL_PREFIX STREQUAL PROJECT_BINARY_DIR)
    install(TARGETS n0s-ryo-miner RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/${EXECUTABLE_OUTPUT_PATH}")
    if(CUDA_FOUND)
        install(TARGETS n0s_cuda_backend LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/${LIBRARY_OUTPUT_PATH}")
    endif()
    if(OpenCL_FOUND)
        install(TARGETS n0s_opencl_backend LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/${LIBRARY_OUTPUT_PATH}")
    endif()
else()
    install(CODE "MESSAGE(\"n0s-ryo-miner installed to folder 'bin'\")")
endif()
