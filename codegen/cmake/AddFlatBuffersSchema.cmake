function(add_flatbuffers_schema SCHEMA_FILE)
    get_filename_component(BASE ${SCHEMA_FILE} NAME_WE)
    add_library(FlatBuffers_${BASE} OBJECT EXCLUDE_FROM_ALL)
    target_add_flatbuffers_schema(FlatBuffers_${BASE} ${SCHEMA_FILE})
endfunction()

function(target_add_flatbuffers_schema TARGET_NAME SCHEMA_FILE)
    get_filename_component(BASE ${SCHEMA_FILE} NAME_WE)
    set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/flatbuffers/${BASE})

    FetchContent_GetProperties(flatbuffers)

    ExternalProject_Get_Property(flatbuffers-host BINARY_DIR)
    set(FLATC_EXECUTABLE ${BINARY_DIR}/flatc)

    add_custom_command(
        OUTPUT
        ${GENERATED_DIR}/${BASE}_generated.h
        COMMAND
        ${FLATC_EXECUTABLE}
        --cpp --scoped-enums
        -o ${GENERATED_DIR}
        ${SCHEMA_FILE}
        DEPENDS
        flatbuffers-host
        ${SCHEMA_FILE}
    )

    target_sources(${TARGET_NAME} PRIVATE ${GENERATED_DIR}/${BASE}_generated.h)
    target_include_directories(${TARGET_NAME} PUBLIC ${flatbuffers_SOURCE_DIR}/include ${GENERATED_DIR})
endfunction()
