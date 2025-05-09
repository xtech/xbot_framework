function(add_service_interface SERVICE_INTERFACE_NAME JSON_FILE)
    add_library(${SERVICE_INTERFACE_NAME} OBJECT EXCLUDE_FROM_ALL)
    target_add_service_interface(${SERVICE_INTERFACE_NAME} ${SERVICE_INTERFACE_NAME} ${JSON_FILE})
endfunction()

function(target_add_service_interface TARGET_NAME SERVICE_INTERFACE_NAME JSON_FILE)
    # generate the output directory, otherwise python will complain when multiple instances are run
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated/include)

    set(COG_BASE_ARGS -m cogapp -d -I ${XBOT_CODEGEN_PATH}/xbot_codegen -Dservice_file=${JSON_FILE})
    set(HEADER_TEMPLATE ${XBOT_CODEGEN_PATH}/templates/ServiceInterfaceTemplate.hpp)
    set(SOURCE_TEMPLATE ${XBOT_CODEGEN_PATH}/templates/ServiceInterfaceTemplate.cpp)
    set(HEADER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/include/${SERVICE_INTERFACE_NAME}Base.hpp)
    set(SOURCE_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/${SERVICE_INTERFACE_NAME}Base.cpp)

    add_custom_command(
        OUTPUT ${HEADER_OUTPUT} ${SOURCE_OUTPUT}
        COMMAND ${Python3_EXECUTABLE} ${COG_BASE_ARGS} -o ${HEADER_OUTPUT} ${HEADER_TEMPLATE}
        COMMAND ${Python3_EXECUTABLE} ${COG_BASE_ARGS} -o ${SOURCE_OUTPUT} ${SOURCE_TEMPLATE}
        DEPENDS ${HEADER_TEMPLATE} ${SOURCE_TEMPLATE} ${XBOT_CODEGEN_PATH}/xbot_codegen/xbot_codegen.py ${JSON_FILE}
        COMMENT "Generating code for service interface ${SERVICE_INTERFACE_NAME}."
    )

    target_sources(${TARGET_NAME} PRIVATE ${SOURCE_OUTPUT} ${HEADER_OUTPUT})
    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated/include)
    target_link_libraries(${TARGET_NAME} PUBLIC xbot-service-interface)
endfunction()
