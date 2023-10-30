cmake_minimum_required(VERSION 3.14)
project(extract_fx)
set(CMAKE_CXX_STANDARD 23)

add_executable(extract_fx extract_fx.cpp)


# Macro which takes a cpi file and generates a cpp file in the selected target.
macro(target_extract_file TARGET FILE)
    set(IN ${CMAKE_CURRENT_SOURCE_DIR}/${FILE}.cpi)
    set(OUT ${CMAKE_CURRENT_SOURCE_DIR}/${FILE}.cpp)
    set(EXEC ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/extract_fx.exe)

    ################################################################
    # Custom command to generate .cpp file from .cpi file
    add_custom_command(OUTPUT "${OUT}"
                       DEPENDS "${IN}" extract_fx
                       COMMAND "${EXEC}" --name extracted_string "${IN}" "${OUT}"
    )

    target_sources("${TARGET}" PRIVATE "${OUT}")
endmacro()



add_executable(format_literal_test format_literal_test.cpi format_literal.h)

target_extract_file(format_literal_test format_literal_test)
