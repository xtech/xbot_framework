function(add_service SERVICE_NAME JSON_FILE)
    add_library(${SERVICE_NAME} OBJECT EXCLUDE_FROM_ALL)
    target_add_service(${SERVICE_NAME} ${SERVICE_NAME} ${JSON_FILE})
endfunction()

function(target_add_service TARGET_NAME SERVICE_NAME JSON_FILE)
    # generate the output directory, otherwise python will complain when multiple instances are run
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated/include)

    set(COG_BASE_ARGS -m cogapp -d -I ${XBOT_CODEGEN_PATH}/xbot_codegen -Dservice_file=${JSON_FILE})
    set(HEADER_TEMPLATE ${XBOT_CODEGEN_PATH}/templates/ServiceTemplate.hpp)
    set(SOURCE_TEMPLATE ${XBOT_CODEGEN_PATH}/templates/ServiceTemplate.cpp)
    set(HEADER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/include/${SERVICE_NAME}Base.hpp)
    set(SOURCE_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/${SERVICE_NAME}Base.cpp)

    if (DEFINED XBOT_SERVICE_EXT)
        list(APPEND COG_BASE_ARGS -Dservice_ext="${XBOT_SERVICE_EXT}")
    endif ()

    add_custom_command(
        OUTPUT ${HEADER_OUTPUT} ${SOURCE_OUTPUT}
        COMMAND ${Python3_EXECUTABLE} ${COG_BASE_ARGS} -o ${HEADER_OUTPUT} ${HEADER_TEMPLATE}
        COMMAND ${Python3_EXECUTABLE} ${COG_BASE_ARGS} -o ${SOURCE_OUTPUT} ${SOURCE_TEMPLATE}
        DEPENDS ${HEADER_TEMPLATE} ${SOURCE_TEMPLATE} ${XBOT_CODEGEN_PATH}/xbot_codegen/xbot_codegen.py ${JSON_FILE}
        COMMENT "Generating code for service ${SERVICE_NAME}."
    )

    target_sources(${TARGET_NAME} PRIVATE ${SOURCE_OUTPUT} ${HEADER_OUTPUT})
    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated/include)
    target_link_libraries(${TARGET_NAME} PUBLIC xbot-service)
endfunction()
