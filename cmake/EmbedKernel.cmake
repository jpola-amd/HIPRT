# EmbedKernel.cmake
# Embeds compiled kernel binary into a C header file
#
# Required variables:
#   INPUT_FILE - Input kernel binary file
#   OUTPUT_COMPRESSED - Output compressed file (if compression enabled)
#   OUTPUT_HEADER - Output C header file
#   DO_COMPRESS - Whether to compress the kernel (ON/OFF)

cmake_minimum_required(VERSION 3.19)

# Read binary file
file(READ "${INPUT_FILE}" kernel_binary HEX)

set(compressed_data "")
set(original_size 0)
set(compressed_size 0)

# Compress if requested
if(DO_COMPRESS)
    message(STATUS "Compressing kernel binary...")
    
    # Use external zstd or CMake file(ARCHIVE_CREATE) for compression
    # For now, we'll use a simple approach with file reading
    file(SIZE "${INPUT_FILE}" original_size)
    
    # Create compressed file using zstd if available
    find_program(ZSTD_EXECUTABLE zstd)
    
    if(ZSTD_EXECUTABLE)
        execute_process(
            COMMAND ${ZSTD_EXECUTABLE} -19 "${INPUT_FILE}" -o "${OUTPUT_COMPRESSED}" -f
            RESULT_VARIABLE result
            OUTPUT_QUIET
            ERROR_QUIET
        )
        
        if(result EQUAL 0 AND EXISTS "${OUTPUT_COMPRESSED}")
            file(SIZE "${OUTPUT_COMPRESSED}" compressed_size)
            file(READ "${OUTPUT_COMPRESSED}" compressed_data HEX)
            message(STATUS "Compressed: ${original_size} -> ${compressed_size} bytes")
        else()
            message(WARNING "zstd compression failed, using uncompressed data")
            set(DO_COMPRESS OFF)
        endif()
    else()
        message(WARNING "zstd not found, using uncompressed data")
        set(DO_COMPRESS OFF)
    endif()
endif()

# Select data to embed
if(DO_COMPRESS AND compressed_data)
    set(data_to_embed "${compressed_data}")
    set(data_size ${compressed_size})
else()
    set(data_to_embed "${kernel_binary}")
    file(SIZE "${INPUT_FILE}" data_size)
endif()

# Convert hex string to C array
string(LENGTH "${data_to_embed}" hex_length)
math(EXPR byte_count "${hex_length} / 2")

set(array_content "")
set(counter 0)

string(REGEX MATCHALL ".." hex_bytes "${data_to_embed}")

foreach(byte ${hex_bytes})
    if(counter EQUAL 0)
        string(APPEND array_content "\n    ")
    endif()
    
    string(APPEND array_content "0x${byte}, ")
    
    math(EXPR counter "${counter} + 1")
    if(counter EQUAL 16)
        set(counter 0)
    endif()
endforeach()

# Generate header file
get_filename_component(var_name "${INPUT_FILE}" NAME_WE)
string(REPLACE "-" "_" var_name "${var_name}")
string(REPLACE "." "_" var_name "${var_name}")

if(DO_COMPRESS)
    set(compressed_flag "true")
else()
    set(compressed_flag "false")
endif()

set(header_content "// Automatically generated - do not edit
// Embedded kernel binary

#pragma once

namespace hiprt {

constexpr unsigned int ${var_name}_size = ${data_size};
constexpr bool ${var_name}_compressed = ${compressed_flag};

alignas(16) const unsigned char ${var_name}_data[] = {${array_content}
};

} // namespace hiprt
")

file(WRITE "${OUTPUT_HEADER}" "${header_content}")
message(STATUS "Kernel embedded in: ${OUTPUT_HEADER} (${data_size} bytes)")
