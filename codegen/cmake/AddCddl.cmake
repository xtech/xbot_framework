function(add_cddl CDDL_NAME CDDL_FILE)
    add_library(ZCBOR_${CDDL_NAME} OBJECT EXCLUDE_FROM_ALL)
    target_add_cddl(ZCBOR_${CDDL_NAME} ${CDDL_NAME} ${CDDL_FILE})
endfunction()

function(target_add_cddl TARGET_NAME CDDL_NAME CDDL_FILE)
    add_custom_command(
            OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/${CDDL_NAME}_encode.c
            ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/${CDDL_NAME}_decode.c
            ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/include/${CDDL_NAME}_encode.h
            ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/include/${CDDL_NAME}_decode.h
            ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/include/${CDDL_NAME}_types.h
            COMMAND
            ${ZCBOR_CODEGEN_EXECUTABLE} code
            -c ${CDDL_FILE}
            --encode --decode
            --short-names
            -t ${CDDL_NAME}
            --output-c ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/${CDDL_NAME}.c
            --output-h ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/include/${CDDL_NAME}.h
            DEPENDS
            ${CDDL_FILE}
    )

    target_sources(${TARGET_NAME} PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/${CDDL_NAME}_encode.c
            ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/${CDDL_NAME}_decode.c
    )
    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated/zcbor/${CDDL_NAME}/include)
    target_link_libraries(${TARGET_NAME} PUBLIC zcbor)
endfunction()
