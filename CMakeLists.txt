cmake_minimum_required(VERSION 3.14)
project(extract_fx)
set(CMAKE_CXX_STANDARD 23)

# Build the extract_fx executable with builtin testing
add_executable(extract_fx extract_fx.cpp)


# Macro which takes a cpi file and generates a cpp file in the selected target.
macro(target_fx_file TARGET FILE)
    set(IN ${CMAKE_CURRENT_SOURCE_DIR}/${FILE})
    set(OUT ${CMAKE_CURRENT_BINARY_DIR}/extracted/${FILE})
    set(EXEC ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/extract_fx)

    ################################################################
    # Custom command to generate .cpp file from .cpi file
    add_custom_command(OUTPUT "${OUT}"
                       MAIN_DEPENDENCY "${IN}"
                       DEPENDS extract_fx
                       COMMAND "${EXEC}" --name extracted_string "${IN}" "${OUT}"
    )

    target_sources("${TARGET}" PRIVATE "${IN}" "${OUT}")
    source_group(Extracted "${OUT}")
endmacro()



add_executable(format_literal_test format_literal.h)
target_include_directories(format_literal_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_fx_file(format_literal_test format_literal_test.cpp)


enable_testing()

add_test(NAME test_fx COMMAND extract_fx --test)
