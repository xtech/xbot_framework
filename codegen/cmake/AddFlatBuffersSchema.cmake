function(add_flatbuffers_schema SCHEMA_FILE)
    get_filename_component(BASE ${SCHEMA_FILE} NAME_WE)
    add_library(FlatBuffers_${BASE} OBJECT EXCLUDE_FROM_ALL)
    target_add_flatbuffers_schema(FlatBuffers_${BASE} ${SCHEMA_FILE})
endfunction()

function(target_add_flatbuffers_schema TARGET_NAME SCHEMA_FILE)
    get_filename_component(BASE ${SCHEMA_FILE} NAME_WE)
    set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/flatbuffers/${BASE})
    file(MAKE_DIRECTORY ${GENERATED_DIR})

    FetchContent_GetProperties(flatcc)
    ExternalProject_Get_Property(flatcc-host BINARY_DIR)

    set(GENERATED_FILES
        ${GENERATED_DIR}/${BASE}_reader.h
        ${GENERATED_DIR}/${BASE}_verifier.h
    )

    add_custom_command(
        OUTPUT
            ${GENERATED_FILES}
        COMMAND
            ${BINARY_DIR}/bin/flatcc
            --reader --common_reader --verifier
            -o ${GENERATED_DIR}
            ${SCHEMA_FILE}
        DEPENDS
            flatcc-host
            ${SCHEMA_FILE}
    )

    target_sources(${TARGET_NAME} PRIVATE ${GENERATED_FILES})
    target_include_directories(${TARGET_NAME} PUBLIC ${flatcc_SOURCE_DIR}/include ${GENERATED_DIR})
    target_link_libraries(${TARGET_NAME} PUBLIC flatccrt)
endfunction()
