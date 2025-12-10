# CompileKernels.cmake
# Compiles HIP kernels using hipcc
#
# Required variables:
#   HIPCC_EXECUTABLE - Path to hipcc compiler
#   HIP_PATH - Path to HIP SDK
#   OFFLOAD_ARCHS - List of GPU architectures to target
#   CUDA_FLAG - CUDA compilation flag (--nvidia or empty)
#   OUTPUT_HIPRT - Output path for HIPRT kernel binary
#   OUTPUT_OROCHI - Output path for Orochi kernel binary
#   SOURCE_DIR - Source directory

cmake_minimum_required(VERSION 3.19)

message(STATUS "Compiling HIPRT kernels with hipcc...")
message(STATUS "  hipcc: ${HIPCC_EXECUTABLE}")
message(STATUS "  HIP_PATH: ${HIP_PATH}")

# OFFLOAD_ARCHS comes as an escaped string - convert back to list
# Remove surrounding quotes if present and convert to list
string(REGEX REPLACE "^\"(.*)\"$" "\\1" OFFLOAD_ARCHS_CLEAN "${OFFLOAD_ARCHS}")
message(STATUS "  Architectures: ${OFFLOAD_ARCHS_CLEAN}")

# Find clang++ for linking
find_program(CLANG_EXECUTABLE
    NAMES clang++ amdclang++
    PATHS "${HIP_PATH}/bin"
    NO_DEFAULT_PATH
)
if(NOT CLANG_EXECUTABLE)
    find_program(CLANG_EXECUTABLE NAMES clang++ amdclang++)
endif()
if(NOT CLANG_EXECUTABLE)
    message(FATAL_ERROR "Could not find clang++ or amdclang++ for linking")
endif()
message(STATUS "  clang++: ${CLANG_EXECUTABLE}")

# Kernel source files
set(HIPRT_KERNEL_LIB_SOURCE "${SOURCE_DIR}/hiprt/impl/hiprt_kernels_bitcode.h")
set(HIPRT_KERNEL_SOURCE "${SOURCE_DIR}/hiprt/impl/hiprt_kernels.h")
set(OROCHI_KERNEL_SOURCE "${SOURCE_DIR}/contrib/Orochi/ParallelPrimitives/RadixSortKernels.h")

# Output paths
get_filename_component(OUTPUT_DIR "${OUTPUT_HIPRT}" DIRECTORY)
set(HIPRT_LIB_BC "${OUTPUT_DIR}/hiprt_lib.bc")

# Common compile flags for bitcode (.bc files)
set(BITCODE_FLAGS
    -x hip
    -std=c++17
    -O3
    -fgpu-rdc
    -c
    --gpu-bundle-output
    -emit-llvm
    -ffast-math
    -parallel-jobs=15
    -I${SOURCE_DIR}
    -I${SOURCE_DIR}/hiprt
    -I${SOURCE_DIR}/contrib/Orochi
    -DHIPRT_BITCODE_LINKING
    -D__HIP_PLATFORM_AMD__
)

# Common compile flags for hipfb (fat binary files)
set(HIPFB_FLAGS
    -x hip
    -std=c++17
    -O3
    --genco
    -ffast-math
    -parallel-jobs=15
    -I${SOURCE_DIR}
    -I${SOURCE_DIR}/hiprt
    -I${SOURCE_DIR}/contrib/Orochi
    -DHIPRT_BITCODE_LINKING
    -D__HIP_PLATFORM_AMD__
)

# Build compile commands
set(BITCODE_CMD
    ${HIPCC_EXECUTABLE}
    ${BITCODE_FLAGS}
    ${OFFLOAD_ARCHS_CLEAN}
)

set(HIPFB_CMD
    ${HIPCC_EXECUTABLE}
    ${HIPFB_FLAGS}
    ${OFFLOAD_ARCHS_CLEAN}
)

# Add CUDA flag if present
if(CUDA_FLAG)
    list(APPEND BITCODE_CMD ${CUDA_FLAG})
    list(APPEND HIPFB_CMD ${CUDA_FLAG})
endif()

# Step 1: Compile HIPRT library bitcodes (for linking with unit tests)
if(EXISTS "${HIPRT_KERNEL_LIB_SOURCE}")
    message(STATUS "Compiling HIPRT library bitcodes...")
    
    execute_process(
        COMMAND ${BITCODE_CMD}
            "${HIPRT_KERNEL_LIB_SOURCE}"
            -o "${HIPRT_LIB_BC}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        WORKING_DIRECTORY "${SOURCE_DIR}"
    )
    
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Failed to compile HIPRT library bitcodes:\n${error}\n${output}")
    else()
        message(STATUS "HIPRT library bitcodes created: ${HIPRT_LIB_BC}")
    endif()
else()
    message(FATAL_ERROR "HIPRT library kernel source not found: ${HIPRT_KERNEL_LIB_SOURCE}")
endif()

# Step 2: Compile HIPRT kernels to hipfb (fat binary for runtime loading)
if(EXISTS "${HIPRT_KERNEL_SOURCE}")
    message(STATUS "Compiling HIPRT kernels to hipfb...")
    
    execute_process(
        COMMAND ${HIPFB_CMD}
            "${HIPRT_KERNEL_SOURCE}"
            -o "${OUTPUT_HIPRT}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        WORKING_DIRECTORY "${SOURCE_DIR}"
    )
    
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Failed to compile HIPRT kernels:\n${error}\n${output}")
    else()
        message(STATUS "HIPRT kernels compiled successfully: ${OUTPUT_HIPRT}")
    endif()
else()
    message(WARNING "HIPRT kernel source not found: ${HIPRT_KERNEL_SOURCE}")
endif()

# Step 3: Compile Orochi kernels to hipfb
if(EXISTS "${OROCHI_KERNEL_SOURCE}")
    message(STATUS "Compiling Orochi kernels to hipfb...")
    
    # Orochi kernels need hip_runtime.h to be included
    execute_process(
        COMMAND ${HIPFB_CMD}
            -include hip/hip_runtime.h
            "${OROCHI_KERNEL_SOURCE}"
            -o "${OUTPUT_OROCHI}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        WORKING_DIRECTORY "${SOURCE_DIR}"
    )
    
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Failed to compile Orochi kernels:\n${error}\n${output}")
    else()
        message(STATUS "Orochi kernels compiled successfully: ${OUTPUT_OROCHI}")
    endif()
else()
    message(WARNING "Orochi kernel source not found: ${OROCHI_KERNEL_SOURCE}")
endif()

# Note: HIPRT_LIB_BC is kept for use by unittest linking
