function(add_service SERVICE_NAME JSON_FILE)
    add_library(${SERVICE_NAME} OBJECT EXCLUDE_FROM_ALL
    )
    target_add_service(${SERVICE_NAME} ${SERVICE_NAME} ${JSON_FILE})
endfunction()

function(target_add_service TARGET_NAME SERVICE_NAME JSON_FILE)
    # generate the output directory, otherwise python will complain when multiple instances are run
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated/include)
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/include/${SERVICE_NAME}Base.hpp ${CMAKE_CURRENT_BINARY_DIR}/generated/${SERVICE_NAME}Base.cpp
            COMMAND ${Python3_EXECUTABLE} -m cogapp -d -I ${XBOT_CODEGEN_PATH}/xbot_codegen -D service_file=${JSON_FILE} -o ${CMAKE_CURRENT_BINARY_DIR}/generated/include/${SERVICE_NAME}Base.hpp ${XBOT_CODEGEN_PATH}/templates/ServiceTemplate.hpp
            COMMAND ${Python3_EXECUTABLE} -m cogapp -d -I ${XBOT_CODEGEN_PATH}/xbot_codegen -D service_file=${JSON_FILE} -o ${CMAKE_CURRENT_BINARY_DIR}/generated/${SERVICE_NAME}Base.cpp ${XBOT_CODEGEN_PATH}/templates/ServiceTemplate.cpp
            DEPENDS ${XBOT_CODEGEN_PATH}/templates/ServiceTemplate.hpp ${XBOT_CODEGEN_PATH}/templates/ServiceTemplate.cpp ${JSON_FILE}
            COMMENT "Generating code for service ${SERVICE_NAME}."
    )

    target_sources(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/${SERVICE_NAME}Base.cpp
            ${CMAKE_CURRENT_BINARY_DIR}/generated/include/${SERVICE_NAME}Base.hpp
    )

    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated/include)
    target_link_libraries(${TARGET_NAME} PUBLIC xbot-service)
endfunction()
