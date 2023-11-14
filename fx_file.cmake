# Macro which takes a cpi file and generates a cpp file in the selected target.
function(target_fx_file TARGET)
    set(EXEC ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/extract_fx)
    
    foreach(FILE ${ARGN})
        set(IN ${CMAKE_CURRENT_SOURCE_DIR}/${FILE})
        set(OUT ${CMAKE_CURRENT_BINARY_DIR}/extracted/${FILE})
    message(STATUS ${IN})
    message(STATUS ${OUT})
        ################################################################
        # Custom command to generate pre-preprocessedd .cpp file in the build directory from .cpp file in the source directory.
        add_custom_command(OUTPUT "${OUT}"
                           MAIN_DEPENDENCY "${IN}"
                           DEPENDS extract_fx
                           COMMAND "${EXEC}" --name extracted_string "${IN}" "${OUT}"
        )

        target_sources("${TARGET}" PRIVATE "${IN}" "${OUT}")
        source_group(Extracted FILES ${CMAKE_CURRENT_BINARY_DIR}/extracted/${FILE})
    endforeach()
endfunction()
