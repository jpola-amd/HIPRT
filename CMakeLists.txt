cmake_minimum_required(VERSION 3.19)
project(HIPRT VERSION 3.0.3)

# ==============================================================================
# CMake Options
# ==============================================================================

option(HIPRT_BAKE_KERNEL "Enable the use of baked kernels source code" OFF)
option(HIPRT_BAKE_COMPILED_KERNEL "Compile and embed kernels as buffer inside binary" OFF)
option(HIPRT_BITCODE "Enable bitcode linking (requires external precompiled kernels)" OFF)
option(HIPRT_PRECOMPILE "Precompile kernels" OFF)
option(HIPRT_COMPILED_COMPRESSION "Enable compression of compiled kernels" ON)
option(HIPRT_BUILD_UNITTEST "Build unit tests" ON)
option(HIPRT_BUILD_HIPRTEW "Build HIPRTEW test" OFF)
option(FORCE_DISABLE_CUDA "Force disable CUDA support" OFF)
set(HIPRT_GPU_ARCHS "" CACHE STRING "GPU architectures to build for (AMD: gfx900,gfx906,etc; NVIDIA: sm_70,sm_75,etc). Leave empty for default.")

# ==============================================================================
# CMake Standards and Settings
# ==============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use standard output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Multi-config generators
foreach(OUTPUTCONFIG Debug Release RelWithDebInfo MinSizeRel DebugGpu)
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin/${OUTPUTCONFIG})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG_UPPER} ${CMAKE_BINARY_DIR}/lib/${OUTPUTCONFIG})
endforeach()

# Build type configurations
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo;DebugGpu" CACHE STRING "Build configurations" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DEBUGGPU "")
set(CMAKE_EXE_LINKER_FLAGS_DEBUGGPU "")

# ==============================================================================
# Platform-Specific Settings
# ==============================================================================

if(WIN32)
    add_compile_definitions(__WINDOWS__)
    add_compile_options(/wd4244 /wd4305 /wd4018 /wd4996 /Zc:__cplusplus /EHsc)
elseif(UNIX)
    add_compile_options(-fvisibility=hidden)
endif()

add_compile_definitions(__USE_HIP__ HIPRT_PUBLIC_REPO GTEST_HAS_TR1_TUPLE=0)

# ==============================================================================
# Find HIP SDK
# ==============================================================================

function(get_hip_version result_version result_path)
    set(hip_command "hipcc")
    
    if(DEFINED HIP_PATH)
        message(STATUS "HIP_PATH defined as CMake parameter: ${HIP_PATH}")
        set(hip_root "${HIP_PATH}")
    elseif(DEFINED ENV{HIP_PATH})
        set(hip_root "$ENV{HIP_PATH}")
        message(STATUS "HIP_PATH from environment: ${hip_root}")
    else()
        message(WARNING "HIP_PATH not defined - will search in PATH")
        set(hip_root "")
    endif()
    
    if(hip_root)
        # Clean trailing slashes
        string(REGEX REPLACE "[/\\]$" "" hip_root "${hip_root}")
        set(${result_path} "${hip_root}" PARENT_SCOPE)
        
        if(WIN32)
            if(EXISTS "${hip_root}/bin/hipcc.exe")
                set(hip_command "${hip_root}/bin/hipcc.exe")
            else()
                set(hip_command "perl" "${hip_root}/bin/hipcc")
            endif()
        else()
            set(hip_command "${hip_root}/bin/hipcc")
        endif()
    endif()
    
    execute_process(
        COMMAND ${hip_command} --version
        OUTPUT_VARIABLE version_output
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    
    string(REGEX MATCH "[0-9]+\\.[0-9]+" version "${version_output}")
    
    if(NOT version)
        set(version "UNKNOWN")
    endif()
    
    message(STATUS "HIP version: ${version}")
    set(${result_version} "${version}" PARENT_SCOPE)
endfunction()

get_hip_version(HIP_VERSION_STR HIP_PATH_DETECTED)

# ==============================================================================
# Version Management
# ==============================================================================

function(configure_version_header in_file out_file version_file)
    if(NOT EXISTS "${version_file}")
        message(FATAL_ERROR "Version file missing: ${version_file}")
    endif()
    
    file(STRINGS "${version_file}" version_lines)
    list(GET version_lines 0 major)
    list(GET version_lines 1 minor)
    list(GET version_lines 2 patch)
    list(GET version_lines 3 hash)
    
    math(EXPR api_version "${major} * 1000 + ${minor}")
    
    # Format version as 5-digit string
    string(LENGTH "${api_version}" version_len)
    if(version_len LESS 5)
        math(EXPR pad_len "5 - ${version_len}")
        string(REPEAT "0" ${pad_len} version_pad)
        set(version_str "${version_pad}${api_version}")
    else()
        set(version_str "${api_version}")
    endif()
    
    set(HIPRT_VERSION_STRING "${version_str}" PARENT_SCOPE)
    
    file(READ "${in_file}" content)
    string(REPLACE "@HIPRT_MAJOR_VERSION@" "${major}" content "${content}")
    string(REPLACE "@HIPRT_MINOR_VERSION@" "${minor}" content "${content}")
    string(REPLACE "@HIPRT_PATCH_VERSION@" "${patch}" content "${content}")
    string(REPLACE "@HIPRT_HASH_VERSION@" "0x${hash}" content "${content}")
    string(REPLACE "@HIPRT_VERSION_STR@" "\"${version_str}\"" content "${content}")
    string(REPLACE "@HIPRT_API_VERSION@" "${api_version}" content "${content}")
    string(REPLACE "@HIP_VERSION_STR@" "\"${HIP_VERSION_STR}\"" content "${content}")
    
    file(WRITE "${out_file}" "${content}")
endfunction()

# Generate version headers
configure_version_header(
    "${CMAKE_CURRENT_SOURCE_DIR}/hiprt/hiprt.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/hiprt/hiprt.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/version.txt"
)
configure_version_header(
    "${CMAKE_CURRENT_SOURCE_DIR}/hiprt/hiprtew.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/hiprt/hiprtew.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/version.txt"
)

set(HIPRT_LIB_NAME "hiprt${HIPRT_VERSION_STRING}")

# ==============================================================================
# CUDA Support
# ==============================================================================

if(NOT FORCE_DISABLE_CUDA)
    include(${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/Orochi/enable_cuew.cmake)
else()
    message(STATUS "CUDA support disabled")
endif()

# ==============================================================================
# CMake Utilities for Kernel Generation
# ==============================================================================

# Stringify function - converts header files to C string literals
function(stringify_file input_file output_file)
    file(READ "${input_file}" file_content)
    
    # Remove comments and process includes
    string(REGEX REPLACE "//[^\n]*\n" "" file_content "${file_content}")
    
    # Convert to C string
    string(REPLACE "\\" "\\\\" file_content "${file_content}")
    string(REPLACE "\"" "\\\"" file_content "${file_content}")
    string(REGEX REPLACE "\n" "\\\\n\"\n\"" file_content "${file_content}")
    
    get_filename_component(var_name "${input_file}" NAME_WE)
    set(output_content "static const char* hip_${var_name} = \n\"${file_content}\";\n")
    
    file(APPEND "${output_file}" "${output_content}")
endfunction()

# Generate kernel args
function(generate_kernel_args input_file output_file)
    file(READ "${input_file}" content)
    get_filename_component(var_name "${input_file}" NAME_WE)
    
    file(APPEND "${output_file}" "#if !defined(HIPRT_LOAD_FROM_STRING) && !defined(HIPRT_BITCODE_LINKING)\n")
    file(APPEND "${output_file}" "    static const char** ${var_name}Args = 0;\n")
    file(APPEND "${output_file}" "#else\n")
    file(APPEND "${output_file}" "    static const char* ${var_name}Args[] = {\n")
    
    # Extract includes
    string(REGEX MATCHALL "#include <[^>]+>" includes "${content}")
    foreach(inc ${includes})
        string(REGEX REPLACE "#include <([^>]+)>" "hip_\\1" inc_name "${inc}")
        string(REPLACE ".h" "" inc_name "${inc_name}")
        file(APPEND "${output_file}" "        ${inc_name},\n")
    endforeach()
    
    file(APPEND "${output_file}" "        hip_${var_name}};\n")
    file(APPEND "${output_file}" "#endif\n\n")
endfunction()

# ==============================================================================
# Kernel Baking Target (String Embedding)
# ==============================================================================

set(BAKED_KERNEL_HEADERS
    hiprt/hiprt_vec.h
    hiprt/hiprt_math.h
    hiprt/impl/Obb.h
    hiprt/impl/Aabb.h
    hiprt/impl/AabbList.h
    hiprt/impl/BvhCommon.h
    hiprt/impl/BvhNode.h
    hiprt/impl/Header.h
    hiprt/impl/QrDecomposition.h
    hiprt/impl/Quaternion.h
    hiprt/impl/Transform.h
    hiprt/impl/Instance.h
    hiprt/impl/InstanceList.h
    hiprt/impl/MortonCode.h
    hiprt/impl/TriangleMesh.h
    hiprt/impl/Triangle.h
    hiprt/impl/BvhBuilderUtil.h
    hiprt/impl/SbvhCommon.h
    hiprt/impl/NodeList.h
    hiprt/impl/BvhConfig.h
    hiprt/impl/MemoryArena.h
    hiprt/hiprt_types.h
    hiprt/hiprt_common.h
    hiprt/impl/hiprt_device_impl.h
    hiprt/hiprt_device.h
)

set(OROCHI_KERNEL_HEADERS
    contrib/Orochi/ParallelPrimitives/RadixSortKernels.h
    contrib/Orochi/ParallelPrimitives/RadixSortConfigs.h
)

set(HIPRT_KERNEL_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/hiprt/cache/Kernels.h")
set(HIPRT_KERNELARGS_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/hiprt/cache/KernelArgs.h")
set(OROCHI_KERNEL_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/ParallelPrimitives/cache/Kernels.h")
set(OROCHI_KERNELARGS_OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/ParallelPrimitives/cache/KernelArgs.h")

if(HIPRT_BAKE_KERNEL OR HIPRT_BITCODE)
    set(GENERATE_BAKED_KERNEL ON)
else()
    set(GENERATE_BAKED_KERNEL OFF)
endif()

if(GENERATE_BAKED_KERNEL)
    # Prepare absolute paths
    set(HIPRT_KERNEL_HEADERS_ABS)
    foreach(header ${BAKED_KERNEL_HEADERS})
        list(APPEND HIPRT_KERNEL_HEADERS_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
    endforeach()
    
    set(OROCHI_KERNEL_HEADERS_ABS)
    foreach(header ${OROCHI_KERNEL_HEADERS})
        list(APPEND OROCHI_KERNEL_HEADERS_ABS "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
    endforeach()
    
    add_custom_command(
        OUTPUT 
            "${HIPRT_KERNEL_OUTPUT}"
            "${HIPRT_KERNELARGS_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E echo "// Automatically generated, do not edit" > "${HIPRT_KERNEL_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E echo "// Automatically generated, do not edit" > "${HIPRT_KERNELARGS_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E echo "\#pragma once" >> "${HIPRT_KERNEL_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E echo "\#pragma once" >> "${HIPRT_KERNELARGS_OUTPUT}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILES="${HIPRT_KERNEL_HEADERS_ABS}"
            -DOUTPUT_KERNEL="${HIPRT_KERNEL_OUTPUT}"
            -DOUTPUT_ARGS="${HIPRT_KERNELARGS_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StringifyKernels.cmake"
        DEPENDS ${HIPRT_KERNEL_HEADERS_ABS}
        COMMENT "Generating baked HIPRT kernels"
        VERBATIM
    )
    
    add_custom_command(
        OUTPUT 
            "${OROCHI_KERNEL_OUTPUT}"
            "${OROCHI_KERNELARGS_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E echo "// Automatically generated, do not edit" > "${OROCHI_KERNEL_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E echo "// Automatically generated, do not edit" > "${OROCHI_KERNELARGS_OUTPUT}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILES="${OROCHI_KERNEL_HEADERS_ABS}"
            -DOUTPUT_KERNEL="${OROCHI_KERNEL_OUTPUT}"
            -DOUTPUT_ARGS="${OROCHI_KERNELARGS_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/StringifyKernels.cmake"
        DEPENDS ${OROCHI_KERNEL_HEADERS_ABS}
        COMMENT "Generating baked Orochi kernels"
        VERBATIM
    )
    
    add_custom_target(bake_kernels
        DEPENDS 
            "${HIPRT_KERNEL_OUTPUT}"
            "${HIPRT_KERNELARGS_OUTPUT}"
            "${OROCHI_KERNEL_OUTPUT}"
            "${OROCHI_KERNELARGS_OUTPUT}"
    )
endif()

# ==============================================================================
# Kernel Precompilation (HIP Compilation)
# ==============================================================================

if(HIPRT_PRECOMPILE)
    # Determine OS postfix
    if(WIN32)
        set(KERNEL_OS_POSTFIX "win")
    else()
        set(KERNEL_OS_POSTFIX "linux")
    endif()
    
    # Output files for precompiled kernels
    set(KERNEL_HIPRT_COMP "${CMAKE_BINARY_DIR}/bin/hiprt${HIPRT_VERSION_STRING}_${HIP_VERSION_STR}_amd.hipfb")
    set(KERNEL_OROCHI_COMP "${CMAKE_BINARY_DIR}/bin/oro_compiled_kernels.hipfb")
    set(KERNEL_UNITTEST_COMP "${CMAKE_BINARY_DIR}/bin/hiprt${HIPRT_VERSION_STRING}_${HIP_VERSION_STR}_precompiled_bitcode_${KERNEL_OS_POSTFIX}.hipfb")
    
    # Kernel source files
    set(BVH_SOURCES
        ${CMAKE_SOURCE_DIR}/hiprt/hiprt_vec.h
        ${CMAKE_SOURCE_DIR}/hiprt/hiprt_math.h
        ${CMAKE_SOURCE_DIR}/hiprt/impl/Aabb.h
        ${CMAKE_SOURCE_DIR}/hiprt/impl/BvhCommon.h
        ${CMAKE_SOURCE_DIR}/hiprt/impl/BvhNode.h
        ${CMAKE_SOURCE_DIR}/hiprt/hiprt_types.h
        ${CMAKE_SOURCE_DIR}/hiprt/hiprt_common.h
    )
    
    # Find hipcc
    if(HIP_PATH_DETECTED)
        if(WIN32)
            find_program(HIPCC_EXECUTABLE
                NAMES hipcc.exe hipcc
                PATHS "${HIP_PATH_DETECTED}/bin"
                NO_DEFAULT_PATH
            )
        else()
            find_program(HIPCC_EXECUTABLE
                NAMES hipcc
                PATHS "${HIP_PATH_DETECTED}/bin"
                NO_DEFAULT_PATH
            )
        endif()
    else()
        find_program(HIPCC_EXECUTABLE NAMES hipcc)
    endif()
    
    if(NOT HIPCC_EXECUTABLE)
        message(FATAL_ERROR "hipcc not found. Please set HIP_PATH.")
    endif()
    
    message(STATUS "Using hipcc: ${HIPCC_EXECUTABLE}")
    
    # Determine GPU architectures based on HIP version
    string(REPLACE "." ";" HIP_VERSION_LIST ${HIP_VERSION_STR})
    list(GET HIP_VERSION_LIST 0 HIP_MAJOR)
    list(GET HIP_VERSION_LIST 1 HIP_MINOR)
    math(EXPR HIP_VERSION_NUM "${HIP_MAJOR} * 10 + ${HIP_MINOR}")
    
    # GPU architectures - use custom list if provided, otherwise use defaults
    if(HIPRT_GPU_ARCHS)
        # User provided custom architectures
        string(REPLACE "," ";" GPU_ARCHS "${HIPRT_GPU_ARCHS}")
        message(STATUS "Using custom GPU architectures: ${GPU_ARCHS}")
    else()
        # Default AMD GPU architectures
        set(GPU_ARCHS "gfx900;gfx906;gfx908;gfx90a;gfx1030;gfx1100;gfx1101;gfx1102")
        message(STATUS "Using default GPU architectures: ${GPU_ARCHS}")
    endif()
    
    # Build offload arch arguments
    set(OFFLOAD_ARCHS)
    foreach(arch ${GPU_ARCHS})
        # Handle both AMD (gfx) and NVIDIA (sm_) architectures
        message(STATUS "Adding architecture: ${arch}")
        if(arch MATCHES "^sm_")
            # NVIDIA architecture - hipcc expects --offload-arch=sm_XX format
            list(APPEND OFFLOAD_ARCHS "--offload-arch=${arch}")
        else()
            # AMD architecture - standard gfx format
            list(APPEND OFFLOAD_ARCHS "--offload-arch=${arch}")
        endif()
    endforeach()
    message(STATUS "Final OFFLOAD_ARCHS: ${OFFLOAD_ARCHS}")
    # CUDA support
    if(NOT FORCE_DISABLE_CUDA)
        set(CUDA_COMPILE_FLAG "--nvidia")
    else()
        set(CUDA_COMPILE_FLAG "")
    endif()
    
    # Custom command to compile kernels using hipcc
    # Escape the list for passing to child CMake script
    string(REPLACE ";" "\\;" OFFLOAD_ARCHS_ESCAPED "${OFFLOAD_ARCHS}")
    
    add_custom_command(
        OUTPUT "${KERNEL_HIPRT_COMP}" "${KERNEL_OROCHI_COMP}"
        COMMAND ${CMAKE_COMMAND}
            -DHIPCC_EXECUTABLE=${HIPCC_EXECUTABLE}
            -DHIP_PATH=${HIP_PATH_DETECTED}
            -DOFFLOAD_ARCHS="${OFFLOAD_ARCHS_ESCAPED}"
            -DCUDA_FLAG=${CUDA_COMPILE_FLAG}
            -DOUTPUT_HIPRT=${KERNEL_HIPRT_COMP}
            -DOUTPUT_OROCHI=${KERNEL_OROCHI_COMP}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileKernels.cmake"
        DEPENDS ${BVH_SOURCES}
        COMMENT "Precompiling HIPRT kernels with hipcc"
        VERBATIM
    )
    
    add_custom_target(precompile_kernels ALL
        DEPENDS "${KERNEL_HIPRT_COMP}" "${KERNEL_OROCHI_COMP}"
    )
    
    # Unittest kernels
    if(HIPRT_BUILD_UNITTEST)
        set(UNITTEST_SOURCES
            ${CMAKE_SOURCE_DIR}/test/bitcodes/custom_func_table.cpp
            ${CMAKE_SOURCE_DIR}/test/bitcodes/unit_test.cpp
        )
        
        # HIPRT lib bitcode path (temporary file created during kernel compilation)
        set(HIPRT_LIB_BC_PATH "${CMAKE_BINARY_DIR}/bin/hiprt_lib.bc")
        
        add_custom_command(
            OUTPUT "${KERNEL_UNITTEST_COMP}"
            COMMAND ${CMAKE_COMMAND}
                -DHIPCC_EXECUTABLE=${HIPCC_EXECUTABLE}
                -DHIP_PATH=${HIP_PATH_DETECTED}
                "-DOFFLOAD_ARCHS=${OFFLOAD_ARCHS_ESCAPED}"
                -DCUDA_FLAG=${CUDA_COMPILE_FLAG}
                -DOUTPUT_UNITTEST=${KERNEL_UNITTEST_COMP}
                -DHIPRT_LIB_BC=${HIPRT_LIB_BC_PATH}
                -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileUnittestKernels.cmake"
            DEPENDS "${KERNEL_HIPRT_COMP}" ${UNITTEST_SOURCES}
            COMMENT "Precompiling unittest kernels"
            VERBATIM
        )
        
        add_custom_target(precompile_unittest_kernels ALL
            DEPENDS "${KERNEL_UNITTEST_COMP}"
        )
    endif()
endif()

# ==============================================================================
# Compiled Kernel Embedding
# ==============================================================================

if(HIPRT_BAKE_COMPILED_KERNEL)
    set(KERNEL_HIPRT_H "${CMAKE_CURRENT_SOURCE_DIR}/hiprt/impl/bvh_build_array.h")
    set(KERNEL_OROCHI_H "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/ParallelPrimitives/cache/oro_compiled_kernels.h")
    
    set(KERNEL_HIPRT_COMP_COMPRESSED "${CMAKE_BINARY_DIR}/hiprt${HIPRT_VERSION_STRING}_${HIP_VERSION_STR}_amd.zstd")
    set(KERNEL_OROCHI_COMP_COMPRESSED "${CMAKE_BINARY_DIR}/oro_compiled_kernels.zstd")
    
    # Convert compiled kernel to header
    add_custom_command(
        OUTPUT "${KERNEL_HIPRT_H}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${KERNEL_HIPRT_COMP}
            -DOUTPUT_COMPRESSED=${KERNEL_HIPRT_COMP_COMPRESSED}
            -DOUTPUT_HEADER=${KERNEL_HIPRT_H}
            -DDO_COMPRESS=${HIPRT_COMPILED_COMPRESSION}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedKernel.cmake"
        DEPENDS "${KERNEL_HIPRT_COMP}"
        COMMENT "Embedding HIPRT compiled kernel"
        VERBATIM
    )
    
    add_custom_command(
        OUTPUT "${KERNEL_OROCHI_H}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${KERNEL_OROCHI_COMP}
            -DOUTPUT_COMPRESSED=${KERNEL_OROCHI_COMP_COMPRESSED}
            -DOUTPUT_HEADER=${KERNEL_OROCHI_H}
            -DDO_COMPRESS=${HIPRT_COMPILED_COMPRESSION}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedKernel.cmake"
        DEPENDS "${KERNEL_OROCHI_COMP}"
        COMMENT "Embedding Orochi compiled kernel"
        VERBATIM
    )
    
    add_custom_target(bake_compiled_kernels ALL
        DEPENDS "${KERNEL_HIPRT_H}" "${KERNEL_OROCHI_H}" precompile_kernels
    )
endif()

# ==============================================================================
# HIPRT Library Target
# ==============================================================================

add_library(${HIPRT_LIB_NAME} SHARED)

# Output name
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(${HIPRT_LIB_NAME} PROPERTIES OUTPUT_NAME "${HIPRT_LIB_NAME}64D")
else()
    set_target_properties(${HIPRT_LIB_NAME} PROPERTIES OUTPUT_NAME "${HIPRT_LIB_NAME}64")
endif()

# Compile definitions
target_compile_definitions(${HIPRT_LIB_NAME} PRIVATE HIPRT_EXPORTS)

if(HIPRT_BITCODE)
    target_compile_definitions(${HIPRT_LIB_NAME} PRIVATE HIPRT_BITCODE_LINKING ORO_PRECOMPILED)
endif()

if(HIPRT_BAKE_KERNEL)
    target_compile_definitions(${HIPRT_LIB_NAME} PRIVATE 
        HIPRT_LOAD_FROM_STRING 
        ORO_PP_LOAD_FROM_STRING
        HIPRT_BAKE_KERNEL_GENERATED
    )
endif()

if(HIPRT_BAKE_COMPILED_KERNEL)
    target_compile_definitions(${HIPRT_LIB_NAME} PRIVATE 
        ORO_PP_LOAD_FROM_STRING 
        HIPRT_BITCODE_LINKING 
        ORO_PRECOMPILED
        HIPRT_BAKE_COMPILED_KERNEL
    )
endif()

# Include directories
target_include_directories(${HIPRT_LIB_NAME} 
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi
)

# Source files
file(GLOB_RECURSE hiprt_sources 
    "${CMAKE_CURRENT_SOURCE_DIR}/hiprt/*.h" 
    "${CMAKE_CURRENT_SOURCE_DIR}/hiprt/*.cpp"
)
list(FILTER hiprt_sources EXCLUDE REGEX "hiprt/bitcodes/.*")

file(GLOB_RECURSE orochi_sources 
    "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/Orochi/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/Orochi/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/contrib/cuew/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/contrib/cuew/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/contrib/hipew/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/contrib/hipew/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/ParallelPrimitives/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/ParallelPrimitives/*.cpp"
)

target_sources(${HIPRT_LIB_NAME} PRIVATE ${hiprt_sources} ${orochi_sources})

# Platform-specific linking
if(WIN32)
    target_link_libraries(${HIPRT_LIB_NAME} PRIVATE version)
endif()

# Zstd for compression
if(HIPRT_BAKE_COMPILED_KERNEL AND HIPRT_COMPILED_COMPRESSION)
    file(GLOB ZSTD_SRCS
        contrib/zstd/lib/common/*.c
        contrib/zstd/lib/decompress/*.c
    )
    
    add_library(zstd_embedded STATIC ${ZSTD_SRCS})
    target_include_directories(zstd_embedded PUBLIC contrib/zstd/lib)
    set_target_properties(zstd_embedded PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(zstd_embedded PRIVATE ZSTD_DISABLE_ASM)
    
    target_link_libraries(${HIPRT_LIB_NAME} PRIVATE zstd_embedded)
    target_compile_definitions(${HIPRT_LIB_NAME} PRIVATE ORO_LINK_ZSTD)
endif()

# Dependencies on kernel generation
if(GENERATE_BAKED_KERNEL)
    add_dependencies(${HIPRT_LIB_NAME} bake_kernels)
endif()

if(HIPRT_BAKE_COMPILED_KERNEL)
    add_dependencies(${HIPRT_LIB_NAME} bake_compiled_kernels)
endif()

if(HIPRT_PRECOMPILE AND HIPRT_BUILD_UNITTEST)
    add_dependencies(${HIPRT_LIB_NAME} precompile_unittest_kernels)
endif()

# ==============================================================================
# Unit Test Target
# ==============================================================================

if(HIPRT_BUILD_UNITTEST)
    add_executable(unittest)
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(unittest PROPERTIES OUTPUT_NAME "unittest64D")
    else()
        set_target_properties(unittest PROPERTIES OUTPUT_NAME "unittest64")
    endif()
    
    if(HIPRT_BITCODE OR HIPRT_BAKE_COMPILED_KERNEL)
        target_compile_definitions(unittest PRIVATE HIPRT_BITCODE_LINKING)
    endif()
    
    if(WIN32)
        target_compile_options(unittest PRIVATE /wd4244)
        target_link_libraries(unittest PRIVATE version)
    endif()
    
    target_include_directories(unittest PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/gtest-1.6.0
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/embree/include
    )
    
    file(GLOB_RECURSE unittest_sources 
        "${CMAKE_CURRENT_SOURCE_DIR}/test/hiprtT*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/hiprtT*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/shared.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/main.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/CornellBox.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/kernels/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/contrib/gtest-1.6.0/gtest-all.cc"
    )
    
    target_sources(unittest PRIVATE ${unittest_sources} ${orochi_sources})
    target_link_libraries(unittest PRIVATE ${HIPRT_LIB_NAME})
    
    if(UNIX)
        target_link_libraries(unittest PRIVATE pthread dl)
    endif()
    
    if(WIN32)
        target_link_directories(unittest PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/contrib/embree/win
        )
        target_link_libraries(unittest PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/contrib/embree/win/embree4.lib
        )
    else()
        target_link_directories(unittest PRIVATE 
            ${CMAKE_CURRENT_SOURCE_DIR}/contrib/embree/linux
        )
    endif()
    
    target_link_libraries(unittest PRIVATE embree4 tbb)
endif()

# ==============================================================================
# HIPRTEW Test Target
# ==============================================================================

if(HIPRT_BUILD_HIPRTEW)
    add_executable(hiprtewtest)
    
    target_compile_definitions(hiprtewtest PRIVATE HIPRT_EXPORTS USE_HIPRTEW)
    
    if(WIN32)
        target_compile_options(hiprtewtest PRIVATE /wd4244)
        target_link_libraries(hiprtewtest PRIVATE version)
    endif()
    
    target_include_directories(hiprtewtest PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi
        ${CMAKE_CURRENT_SOURCE_DIR}/contrib/gtest-1.6.0
    )
    
    if(UNIX)
        target_link_libraries(hiprtewtest PRIVATE pthread dl)
    endif()
    
    file(GLOB_RECURSE hiprtewtest_sources 
        "${CMAKE_CURRENT_SOURCE_DIR}/test/hiprtewTest.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/hiprtewTest.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/contrib/gtest-1.6.0/gtest-all.cc"
    )
    
    target_sources(hiprtewtest PRIVATE ${hiprtewtest_sources} ${orochi_sources})
endif()

# ==============================================================================
# Installation Rules
# ==============================================================================

include(GNUInstallDirs)

# Install library
install(TARGETS ${HIPRT_LIB_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/hiprt/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiprt
    FILES_MATCHING PATTERN "*.h"
    PATTERN "cache" EXCLUDE
    PATTERN "bitcodes" EXCLUDE
    PATTERN "*.in" EXCLUDE
    PATTERN "impl/bvh_build_array.h" EXCLUDE
)

# Install Orochi headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/contrib/Orochi/ParallelPrimitives/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/contrib/Orochi/ParallelPrimitives
    FILES_MATCHING PATTERN "*.h"
    PATTERN "cache" EXCLUDE
)

# Install precompiled kernels
if(HIPRT_PRECOMPILE AND NOT HIPRT_BAKE_COMPILED_KERNEL)
    install(FILES 
        "${KERNEL_HIPRT_COMP}"
        "${KERNEL_OROCHI_COMP}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# ==============================================================================
# Package Configuration
# ==============================================================================

include(CMakePackageConfigHelpers)

# Generate package config file
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/HIPRTConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/HIPRTConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HIPRT
)

# Generate version file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/HIPRTConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install package config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/HIPRTConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/HIPRTConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HIPRT
)
