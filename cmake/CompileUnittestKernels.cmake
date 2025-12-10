# CompileUnittestKernels.cmake
# Compiles unittest kernels using hipcc
#
# Required variables:
#   HIPCC_EXECUTABLE - Path to hipcc compiler
#   HIP_PATH - Path to HIP SDK
#   OFFLOAD_ARCHS - List of GPU architectures to target
#   CUDA_FLAG - CUDA compilation flag (--nvidia or empty)
#   OUTPUT_UNITTEST - Output path for unittest kernel binary
#   SOURCE_DIR - Source directory

cmake_minimum_required(VERSION 3.19)

message(STATUS "Compiling unittest kernels with hipcc...")

# OFFLOAD_ARCHS comes as an escaped string - convert back to list
string(REGEX REPLACE "^\"(.*)\"$" "\\1" OFFLOAD_ARCHS_CLEAN "${OFFLOAD_ARCHS}")

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

# Unittest kernel sources
set(CUSTOM_FUNC_SOURCE "${SOURCE_DIR}/test/bitcodes/custom_func_table.cpp")
set(UNIT_TEST_SOURCE "${SOURCE_DIR}/test/bitcodes/unit_test.cpp")

# Temporary bitcode files
get_filename_component(OUTPUT_DIR "${OUTPUT_UNITTEST}" DIRECTORY)
set(CUSTOM_FUNC_BC "${OUTPUT_DIR}/custom_func_table.bc")
set(UNIT_TEST_BC "${OUTPUT_DIR}/unit_test.bc")

# Common compile flags for bitcode
set(COMMON_FLAGS
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
    -I${SOURCE_DIR}/test
    -DHIPRT_BITCODE_LINKING
    -D__HIP_PLATFORM_AMD__
    -DBLOCK_SIZE=64
    -DSHARED_STACK_SIZE=16
)

# Build compile command
set(COMPILE_CMD
    ${HIPCC_EXECUTABLE}
    ${COMMON_FLAGS}
    ${OFFLOAD_ARCHS_CLEAN}
)

# Add CUDA flag if present
if(CUDA_FLAG)
    list(APPEND COMPILE_CMD ${CUDA_FLAG})
endif()

# Step 1: Compile custom function table bitcodes
if(EXISTS "${CUSTOM_FUNC_SOURCE}")
    message(STATUS "Compiling custom function table bitcodes...")
    
    execute_process(
        COMMAND ${COMPILE_CMD}
            "${CUSTOM_FUNC_SOURCE}"
            -o "${CUSTOM_FUNC_BC}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        WORKING_DIRECTORY "${SOURCE_DIR}"
    )
    
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Failed to compile custom function table:\n${error}\n${output}")
    endif()
else()
    message(WARNING "Custom function source not found: ${CUSTOM_FUNC_SOURCE}")
endif()

# Step 2: Compile unit test bitcodes
if(EXISTS "${UNIT_TEST_SOURCE}")
    message(STATUS "Compiling unit test bitcodes...")
    
    execute_process(
        COMMAND ${COMPILE_CMD}
            "${UNIT_TEST_SOURCE}"
            -o "${UNIT_TEST_BC}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        WORKING_DIRECTORY "${SOURCE_DIR}"
    )
    
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Failed to compile unit test bitcodes:\n${error}\n${output}")
    endif()
else()
    message(WARNING "Unit test source not found: ${UNIT_TEST_SOURCE}")
endif()

# Step 3: Link unittest bitcodes with HIPRT library
message(STATUS "Linking unittest bitcodes with HIPRT library...")

# Find the HIPRT library bitcode (should have been created by CompileKernels.cmake)
get_filename_component(HIPRT_LIB_DIR "${HIPRT_LIB_BC}" DIRECTORY)
if(NOT EXISTS "${HIPRT_LIB_BC}")
    message(WARNING "HIPRT library bitcode not found at: ${HIPRT_LIB_BC}")
    message(WARNING "Attempting to link without it...")
    set(LINK_INPUTS "${CUSTOM_FUNC_BC}" "${UNIT_TEST_BC}")
else()
    set(LINK_INPUTS "${CUSTOM_FUNC_BC}" "${UNIT_TEST_BC}" "${HIPRT_LIB_BC}")
endif()

execute_process(
    COMMAND ${CLANG_EXECUTABLE}
        -fgpu-rdc
        --hip-link
        --cuda-device-only
        ${OFFLOAD_ARCHS_CLEAN}
        ${LINK_INPUTS}
        -o "${OUTPUT_UNITTEST}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    WORKING_DIRECTORY "${SOURCE_DIR}"
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Failed to link unittest bitcodes:\n${error}\n${output}")
else()
    message(STATUS "Unittest bitcodes linked successfully: ${OUTPUT_UNITTEST}")
    # Clean up temporary files
    file(REMOVE "${CUSTOM_FUNC_BC}" "${UNIT_TEST_BC}")
endif()
