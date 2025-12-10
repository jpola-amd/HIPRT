# StringifyKernels.cmake
# Converts header files to C string literals for kernel embedding
#
# Required variables:
#   INPUT_FILES - List of input header files
#   OUTPUT_KERNEL - Output file for kernel strings
#   OUTPUT_ARGS - Output file for kernel arguments

cmake_minimum_required(VERSION 3.19)

# Process input files list
string(REPLACE ";" "\\;" INPUT_FILES_LIST "${INPUT_FILES}")
string(REPLACE "\\" "/" INPUT_FILES_LIST "${INPUT_FILES_LIST}")

# Convert to CMake list
set(FILES_TO_PROCESS)
string(REGEX MATCHALL "[^;]+" FILES_TO_PROCESS "${INPUT_FILES_LIST}")

# Function to stringify a file
function(stringify_file input_file output_file)
    file(READ "${input_file}" file_content)
    
    # Get variable name from file
    get_filename_component(var_name "${input_file}" NAME_WE)
    
    # Remove C++ style comments
    string(REGEX REPLACE "//[^\n]*\n" "\n" file_content "${file_content}")
    
    # Escape backslashes and quotes
    string(REPLACE "\\" "\\\\" file_content "${file_content}")
    string(REPLACE "\"" "\\\"" file_content "${file_content}")
    
    # Split into lines and add quotes
    string(REGEX REPLACE "\n" "\\\\n\"\n\"" file_content "${file_content}")
    
    # Write output
    set(output_content "static const char* hip_${var_name} = \n\"${file_content}\";\n\n")
    file(APPEND "${output_file}" "${output_content}")
endfunction()

# Function to generate kernel args
function(generate_kernel_args input_file output_file)
    file(READ "${input_file}" content)
    get_filename_component(var_name "${input_file}" NAME_WE)
    
    file(APPEND "${output_file}" "#if !defined(HIPRT_LOAD_FROM_STRING) && !defined(HIPRT_BITCODE_LINKING)\n")
    file(APPEND "${output_file}" "static const char** ${var_name}Args = 0;\n")
    file(APPEND "${output_file}" "#else\n")
    file(APPEND "${output_file}" "static const char* ${var_name}Args[] = {\n")
    
    # Extract includes
    string(REGEX MATCHALL "#include[ \t]*<[^>]+>" includes "${content}")
    foreach(inc ${includes})
        string(REGEX REPLACE "#include[ \t]*<([^>]+)>" "\\1" inc_name "${inc}")
        string(REPLACE ".h" "" inc_base "${inc_name}")
        string(REPLACE "/" "_" inc_base "${inc_base}")
        file(APPEND "${output_file}" "    hip_${inc_base},\n")
    endforeach()
    
    file(APPEND "${output_file}" "    hip_${var_name}};\n")
    file(APPEND "${output_file}" "#endif\n\n")
endfunction()

# Process all files
file(APPEND "${OUTPUT_KERNEL}" "namespace hip {\n\n")
file(APPEND "${OUTPUT_ARGS}" "namespace hip {\n\n")

foreach(input_file ${FILES_TO_PROCESS})
    if(EXISTS "${input_file}")
        message(STATUS "Stringifying: ${input_file}")
        stringify_file("${input_file}" "${OUTPUT_KERNEL}")
        generate_kernel_args("${input_file}" "${OUTPUT_ARGS}")
    else()
        message(WARNING "File not found: ${input_file}")
    endif()
endforeach()

file(APPEND "${OUTPUT_KERNEL}" "} // namespace hip\n")
file(APPEND "${OUTPUT_ARGS}" "} // namespace hip\n")

message(STATUS "Kernel stringification complete")
