# Tests for xbot-service-interface. Enable with -DXBOT_BUILD_TESTS=ON (the CI preset does), run with ctest.

# The interface generated from the template schema. It uses every type the generator knows (blob,
# string), so building it is what compiles that generated code in CI.
add_service_interface(ServiceTemplateInterface ${XBOT_CODEGEN_PATH}/templates/service.json)

find_package(Threads REQUIRED)

add_executable(InterfaceCodegenTests InterfaceCodegenTests.cpp)
target_compile_options(InterfaceCodegenTests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(InterfaceCodegenTests PRIVATE ServiceTemplateInterface Threads::Threads)

add_test(NAME xbot-service-interface-codegen COMMAND InterfaceCodegenTests)
# The calls wait for answers from another thread; a lost one must fail the run, not hang it.
set_tests_properties(xbot-service-interface-codegen PROPERTIES TIMEOUT 60)
